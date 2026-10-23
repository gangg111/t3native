package com.t3native.tekken3;

import android.Manifest;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.wifi.WpsInfo;
import android.net.wifi.p2p.WifiP2pConfig;
import android.net.wifi.p2p.WifiP2pManager;
import android.net.wifi.p2p.nsd.WifiP2pDnsSdServiceInfo;
import android.net.wifi.p2p.nsd.WifiP2pDnsSdServiceRequest;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * NETWORK MODE over Wi-Fi Direct (this APK's variant): two phones with no router or hotspot.
 *
 * The host makes its own group (it is the group owner, 192.168.49.1) and advertises the game as a
 * DNS-SD service of type _t3native._udp named after the phone; the guest looks for that service, lists
 * the hosts it finds, and joins one (WifiP2pManager.connect, the host's phone may ask its player to
 * accept). Once in the group the game's own UDP protocol runs as on any local network: the guest
 * joins the group owner's address (address()).
 *
 * Phones already joined in a group (through the system's Wi-Fi Direct settings) keep it: the host
 * that owns one advertises in it, and a guest in one lists its owner as GROUP HOST at once. Only a
 * group this app made is removed again (ownGroup).
 *
 * Every step goes to the log and to the network log in Downloads (t3native_net_<time>.txt).
 *
 * Called from the game thread; every WifiP2pManager call runs on the main thread, whose looper
 * gets the answers. The state the game reads (hosts(), address(), note()) is synchronized.
 */
final class WifiDirect {
    private static final String TAG = "t3native";
    private static final String SERVICE = "_t3native._udp";
    private static final String GROUP_HOST = "@group";   // hosts(): the owner of the group this phone is in
    private static final long REFRESH_MS = 10_000;    // discovery stops by itself after a while: again
    private static final int PERMISSION_REQUEST = 27030;
    private static final int RETRIES = 8;             // a BUSY answer (the framework still busy) tried again
    private static final long RETRY_MS = 1500;

    private final Activity activity;
    private final Handler main = new Handler(Looper.getMainLooper());
    private WifiP2pManager manager;
    private WifiP2pManager.Channel channel;
    private BroadcastReceiver receiver;

    private enum Role { NONE, HOST, GUEST }
    private Role role = Role.NONE;
    private String name = "";
    private Runnable afterPermission;                  // what waits for the player's answer
    private boolean ownGroup;                          // the group was made (or joined) by this app
    private String groupOwner = "";                    // GUEST: the owner of a group already joined

    // What the game reads.
    private final Map<String, String> hosts = new LinkedHashMap<>();   // device address -> name
    private String address = "";
    private String note = "";

    WifiDirect(Activity activity) {
        this.activity = activity;
    }

    // ---- the game thread's calls

    synchronized String hosts() {
        StringBuilder s = new StringBuilder();
        for (Map.Entry<String, String> e : hosts.entrySet()) s.append(e.getKey()).append('\t').append(e.getValue()).append('\n');
        return s.toString();
    }

    synchronized String address() {
        return address;
    }

    synchronized String note() {
        return note;
    }

    void host(final String hostName) {
        main.post(() -> {
            stopNow();
            role = Role.HOST;
            name = hostName;
            log("host " + hostName);
            withPermission(this::startHost);
        });
    }

    void search() {
        main.post(() -> {
            stopNow();
            role = Role.GUEST;
            log("search");
            withPermission(this::startSearch);
        });
    }

    void connect(final String deviceAddress) {
        main.post(() -> {
            if (role != Role.GUEST || manager == null) return;
            if (GROUP_HOST.equals(deviceAddress)) {
                // Already in the host's group: its address is all the game needs.
                log("join the group's owner " + groupOwner);
                setNote("");
                setAddress(groupOwner);
                return;
            }
            main.removeCallbacks(refresh);
            manager.stopPeerDiscovery(channel, quiet());   // a scan running makes connect BUSY
            // The host's phone may ask its player to accept the connection.
            setNote("ACCEPT ON PLAYER 1 PHONE");
            WifiP2pConfig config = new WifiP2pConfig();
            config.deviceAddress = deviceAddress;
            config.groupOwnerIntent = 0;      // the host is the group owner
            config.wps.setup = WpsInfo.PBC;
            log("connect to " + deviceAddress);
            ownGroup = true;
            call("connect", l -> manager.connect(channel, config, l), null);
        });
    }

    void stop() {
        main.post(this::stopNow);
    }

    /** The session began: no more discovery (scans on the group's channel would delay the game). */
    void settle() {
        main.post(() -> {
            main.removeCallbacks(refresh);
            if (manager != null && channel != null) manager.stopPeerDiscovery(channel, quiet());
            log("session: discovery stopped");
        });
    }

    /** The answer to the permission dialog (GameActivity.onRequestPermissionsResult). */
    void permissionAnswered(int requestCode, int[] results) {
        if (requestCode != PERMISSION_REQUEST) return;
        boolean granted = results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED;
        log("permission " + (granted ? "granted" : "refused"));
        Runnable then = afterPermission;
        afterPermission = null;
        if (granted && then != null) {
            setNote("");
            then.run();
        } else if (!granted) {
            setNote("ALLOW NEARBY DEVICES");
        }
    }

    // ---- the main thread's work

    private String permission() {
        return Build.VERSION.SDK_INT >= 33 ? Manifest.permission.NEARBY_WIFI_DEVICES : Manifest.permission.ACCESS_FINE_LOCATION;
    }

    private void withPermission(Runnable then) {
        // Android 12 and older find no phones with the location turned off, and say nothing.
        if (Build.VERSION.SDK_INT < 33 && Build.VERSION.SDK_INT >= 28) {
            LocationManager location = (LocationManager) activity.getSystemService(Context.LOCATION_SERVICE);
            if (location != null && !location.isLocationEnabled()) setNote("TURN ON LOCATION");
        }
        if (activity.checkSelfPermission(permission()) == PackageManager.PERMISSION_GRANTED) {
            then.run();
            return;
        }
        afterPermission = then;
        setNote("ALLOW NEARBY DEVICES");
        activity.requestPermissions(new String[] {permission()}, PERMISSION_REQUEST);
    }

    private boolean open() {
        if (manager != null) return true;
        manager = (WifiP2pManager) activity.getApplicationContext().getSystemService(Context.WIFI_P2P_SERVICE);
        if (manager == null) {
            log("no WifiP2pManager");
            setNote("NO WI-FI DIRECT");
            return false;
        }
        channel = manager.initialize(activity.getApplicationContext(), Looper.getMainLooper(), () -> {
            log("channel lost");
            channel = null;
            manager = null;
        });
        IntentFilter filter = new IntentFilter();
        filter.addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION);
        filter.addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION);
        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                onBroadcast(intent);
            }
        };
        if (Build.VERSION.SDK_INT >= 33) activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        else activity.registerReceiver(receiver, filter);
        return true;
    }

    private void onBroadcast(Intent intent) {
        String action = intent.getAction();
        if (WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION.equals(action)) {
            int state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1);
            log("Wi-Fi Direct " + (state == WifiP2pManager.WIFI_P2P_STATE_ENABLED ? "on" : "off"));
            if (state != WifiP2pManager.WIFI_P2P_STATE_ENABLED) setNote("TURN ON WI-FI");
            else if ("TURN ON WI-FI".equals(note())) setNote("");
        } else if (WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION.equals(action) && manager != null && channel != null) {
            manager.requestConnectionInfo(channel, info -> {
                if (info == null || !info.groupFormed) {
                    log("no group");
                    setAddress("");
                    return;
                }
                String go = info.groupOwnerAddress != null ? info.groupOwnerAddress.getHostAddress() : "";
                log("group: " + (info.isGroupOwner ? "this phone owns it" : "owner at " + go));
                if (role == Role.GUEST && !info.isGroupOwner && ownGroup && !go.isEmpty()) {
                    setNote("");
                    setAddress(go);
                }
            });
        }
    }

    private void startHost() {
        if (!open()) return;
        // A group this phone already owns (the system's Wi-Fi Direct settings, an earlier session) is
        // kept and the game advertised in it; a group where it is a guest goes, then a new one.
        manager.requestGroupInfo(channel, group -> {
            if (group != null && group.isGroupOwner()) {
                log("host in the group already owned (" + group.getNetworkName() + ")");
                advertise();
                return;
            }
            if (group != null) {
                log("leave the group of " + (group.getOwner() != null ? group.getOwner().deviceName : "?") + " to host");
                manager.removeGroup(channel, quiet());
            }
            main.postDelayed(() -> call("createGroup", l -> manager.createGroup(channel, l), () -> {
                ownGroup = true;
                log("group made");
                advertise();
            }), group != null ? 1000 : 0);
        });
    }

    private void advertise() {
        Map<String, String> record = new HashMap<>();
        record.put("n", name);
        WifiP2pDnsSdServiceInfo info = WifiP2pDnsSdServiceInfo.newInstance(name, SERVICE, record);
        manager.clearLocalServices(channel, quiet());
        call("addLocalService", l -> manager.addLocalService(channel, info, l), () -> log("game advertised"));
        refresh.run();
    }

    private void startSearch() {
        if (!open()) return;
        synchronized (this) {
            hosts.clear();
            address = "";
        }
        groupOwner = "";
        // Already a guest in a group (joined through the system's settings): its owner is listed.
        manager.requestConnectionInfo(channel, info -> {
            if (info != null && info.groupFormed && !info.isGroupOwner && info.groupOwnerAddress != null) {
                groupOwner = info.groupOwnerAddress.getHostAddress();
                log("already in a group, its owner at " + groupOwner);
                synchronized (this) {
                    hosts.put(GROUP_HOST, "GROUP HOST");
                }
            } else if (info != null && info.groupFormed) {
                // This phone owns a group. With the other phone in it, that phone hosts there (the local
                // search finds it); an empty one is left over (an earlier try) and keeps this phone
                // from joining the host's group: it goes.
                manager.requestGroupInfo(channel, group -> {
                    if (group != null && group.getClientList().isEmpty()) {
                        log("this phone owns an empty group (" + group.getNetworkName() + "), left over: removed");
                        manager.removeGroup(channel, quiet());
                    } else {
                        log("this phone owns a group with " + (group != null ? group.getClientList().size() : 0)
                                + " phone(s) in it: the other phone hosts there (found by the local search)");
                    }
                });
            }
        });
        manager.setDnsSdResponseListeners(channel, (instance, type, device) -> {
            if (type != null && type.startsWith(SERVICE) && device != null) {
                synchronized (this) {
                    if (!hosts.containsKey(device.deviceAddress)) log("game " + instance + " at " + device.deviceAddress);
                    hosts.put(device.deviceAddress, instance);
                }
            }
        }, (domain, record, device) -> {
            if (domain != null && domain.contains(SERVICE) && record != null && record.get("n") != null && device != null) {
                synchronized (this) {
                    hosts.put(device.deviceAddress, record.get("n"));
                }
            }
        });
        manager.clearServiceRequests(channel, quiet());
        call("addServiceRequest", l -> manager.addServiceRequest(channel, WifiP2pDnsSdServiceRequest.newInstance(), l), null);
        refresh.run();
    }

    // Discovery again every REFRESH_MS: the host stays findable, the guest keeps asking for the service.
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            if (manager == null || role == Role.NONE) return;
            manager.discoverPeers(channel, quiet());
            if (role == Role.GUEST) {
                manager.discoverServices(channel, new WifiP2pManager.ActionListener() {
                    @Override
                    public void onSuccess() {
                    }

                    @Override
                    public void onFailure(int reason) {
                        log("discoverServices: " + reasonName(reason));
                        // The request fell out (some phones drop it): asked for again.
                        if (reason == WifiP2pManager.NO_SERVICE_REQUESTS && manager != null) {
                            manager.addServiceRequest(channel, WifiP2pDnsSdServiceRequest.newInstance(), quiet());
                        }
                    }
                });
            }
            main.postDelayed(this, REFRESH_MS);
        }
    };

    private void stopNow() {
        main.removeCallbacks(refresh);
        afterPermission = null;
        if (manager != null && channel != null) {
            manager.stopPeerDiscovery(channel, quiet());
            manager.clearServiceRequests(channel, quiet());
            manager.clearLocalServices(channel, quiet());
            if (ownGroup) {
                log("group removed");
                manager.removeGroup(channel, quiet());
            }
        }
        ownGroup = false;
        groupOwner = "";
        if (receiver != null) {
            try {
                activity.unregisterReceiver(receiver);
            } catch (IllegalArgumentException e) {
                // Already gone.
            }
            receiver = null;
        }
        manager = null;
        channel = null;
        role = Role.NONE;
        synchronized (this) {
            hosts.clear();
            address = "";
            note = "";
        }
    }

    private interface Call {
        void with(WifiP2pManager.ActionListener listener);
    }

    private void call(final String what, final Call call, final Runnable then) {
        call(what, RETRIES, call, then);
    }

    /** A WifiP2pManager call, tried again on BUSY; `then` after it succeeds. */
    private void call(final String what, final int tries, final Call call, final Runnable then) {
        if (manager == null) return;
        call.with(new WifiP2pManager.ActionListener() {
            @Override
            public void onSuccess() {
                log(what + ": done");
                if (then != null) then.run();
            }

            @Override
            public void onFailure(int reason) {
                log(what + ": " + reasonName(reason) + (reason == WifiP2pManager.BUSY && tries > 0 ? ", again" : ""));
                if (reason == WifiP2pManager.BUSY && tries > 0) {
                    main.postDelayed(() -> call(what, tries - 1, call, then), RETRY_MS);
                } else if (reason == WifiP2pManager.P2P_UNSUPPORTED) {
                    setNote("NO WI-FI DIRECT");
                } else if (reason == WifiP2pManager.BUSY) {
                    // Another app or the system's Wi-Fi Direct screen holds it.
                    setNote("WI-FI DIRECT BUSY, TRY AGAIN");
                } else {
                    setNote("WI-FI DIRECT FAILED");
                }
            }
        });
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case WifiP2pManager.ERROR: return "ERROR";
            case WifiP2pManager.P2P_UNSUPPORTED: return "P2P_UNSUPPORTED";
            case WifiP2pManager.BUSY: return "BUSY";
            case WifiP2pManager.NO_SERVICE_REQUESTS: return "NO_SERVICE_REQUESTS";
            default: return "reason " + reason;
        }
    }

    private WifiP2pManager.ActionListener quiet() {
        return new WifiP2pManager.ActionListener() {
            @Override
            public void onSuccess() {
            }

            @Override
            public void onFailure(int reason) {
            }
        };
    }

    /** A step of Wi-Fi Direct: the system log and the network log in Downloads (GameActivity.netLine). */
    private void log(String text) {
        String line = "net: Wi-Fi Direct " + text;
        Log.i(TAG, line);
        if (activity instanceof GameActivity) ((GameActivity) activity).netLine(line);
    }

    private synchronized void setNote(String text) {
        note = text;
    }

    private synchronized void setAddress(String text) {
        address = text;
    }
}
