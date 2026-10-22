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
 * Called from the game thread; every WifiP2pManager call runs on the main thread, whose looper
 * gets the answers. The state the game reads (hosts(), address(), note()) is synchronized.
 */
final class WifiDirect {
    private static final String TAG = "t3native";
    private static final String SERVICE = "_t3native._udp";
    private static final long REFRESH_MS = 10_000;    // discovery stops by itself after a while: again
    private static final int PERMISSION_REQUEST = 27030;
    private static final int RETRIES = 3;             // a BUSY answer (the framework still busy) tried again

    private final Activity activity;
    private final Handler main = new Handler(Looper.getMainLooper());
    private WifiP2pManager manager;
    private WifiP2pManager.Channel channel;
    private BroadcastReceiver receiver;

    private enum Role { NONE, HOST, GUEST }
    private Role role = Role.NONE;
    private String name = "";
    private Runnable afterPermission;                  // what waits for the player's answer

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
            withPermission(this::startHost);
        });
    }

    void search() {
        main.post(() -> {
            stopNow();
            role = Role.GUEST;
            withPermission(this::startSearch);
        });
    }

    void connect(final String deviceAddress) {
        main.post(() -> {
            if (role != Role.GUEST || manager == null) return;
            main.removeCallbacks(refresh);
            manager.stopPeerDiscovery(channel, quiet());   // a scan running makes connect BUSY
            // The host's phone may ask its player to accept the connection.
            setNote("ACCEPT ON PLAYER 1 PHONE");
            WifiP2pConfig config = new WifiP2pConfig();
            config.deviceAddress = deviceAddress;
            config.groupOwnerIntent = 0;      // the host is the group owner
            config.wps.setup = WpsInfo.PBC;
            Log.i(TAG, "net: Wi-Fi Direct connect to " + deviceAddress);
            call("connect", RETRIES, l -> manager.connect(channel, config, l), null);
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
        });
    }

    /** The answer to the permission dialog (GameActivity.onRequestPermissionsResult). */
    void permissionAnswered(int requestCode, int[] results) {
        if (requestCode != PERMISSION_REQUEST) return;
        boolean granted = results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED;
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
            setNote("NO WI-FI DIRECT");
            return false;
        }
        channel = manager.initialize(activity.getApplicationContext(), Looper.getMainLooper(), () -> {
            Log.i(TAG, "net: Wi-Fi Direct channel lost");
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
            if (state != WifiP2pManager.WIFI_P2P_STATE_ENABLED) setNote("TURN ON WI-FI");
            else if ("TURN ON WI-FI".equals(note())) setNote("");
        } else if (WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION.equals(action) && manager != null && channel != null) {
            manager.requestConnectionInfo(channel, info -> {
                if (info == null || !info.groupFormed) {
                    setAddress("");
                    return;
                }
                if (role == Role.GUEST && !info.isGroupOwner && info.groupOwnerAddress != null) {
                    String goAddress = info.groupOwnerAddress.getHostAddress();
                    Log.i(TAG, "net: Wi-Fi Direct group formed, host at " + goAddress);
                    setNote("");
                    setAddress(goAddress);
                } else if (role == Role.HOST && info.isGroupOwner) {
                    Log.i(TAG, "net: Wi-Fi Direct group up as its owner");
                }
            });
        }
    }

    private void startHost() {
        if (!open()) return;
        // An old group of this app (an earlier session) first, then this one.
        manager.removeGroup(channel, quiet());
        main.postDelayed(() -> call("createGroup", RETRIES, l -> manager.createGroup(channel, l), () -> {
            Map<String, String> record = new HashMap<>();
            record.put("n", name);
            WifiP2pDnsSdServiceInfo info = WifiP2pDnsSdServiceInfo.newInstance(name, SERVICE, record);
            manager.clearLocalServices(channel, quiet());
            call("addLocalService", RETRIES, l -> manager.addLocalService(channel, info, l), null);
            refresh.run();
        }), 300);
    }

    private void startSearch() {
        if (!open()) return;
        synchronized (this) {
            hosts.clear();
            address = "";
        }
        manager.setDnsSdResponseListeners(channel, (instance, type, device) -> {
            if (type != null && type.startsWith(SERVICE) && device != null) {
                synchronized (this) {
                    if (!hosts.containsKey(device.deviceAddress)) Log.i(TAG, "net: Wi-Fi Direct game " + instance + " at " + device.deviceAddress);
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
        call("addServiceRequest", RETRIES, l -> manager.addServiceRequest(channel, WifiP2pDnsSdServiceRequest.newInstance(), l), null);
        refresh.run();
    }

    // Discovery again every REFRESH_MS: the host stays findable, the guest keeps asking for the service.
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            if (manager == null || role == Role.NONE) return;
            manager.discoverPeers(channel, quiet());
            if (role == Role.GUEST) manager.discoverServices(channel, quiet());
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
            if (role != Role.NONE) manager.removeGroup(channel, quiet());
        }
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

    /** A WifiP2pManager call, tried again on BUSY; `then` after it succeeds. */
    private void call(final String what, final int tries, final Call call, final Runnable then) {
        if (manager == null) return;
        call.with(new WifiP2pManager.ActionListener() {
            @Override
            public void onSuccess() {
                if (then != null) then.run();
            }

            @Override
            public void onFailure(int reason) {
                Log.i(TAG, "net: Wi-Fi Direct " + what + " failed (" + reason + ")");
                if (reason == WifiP2pManager.BUSY && tries > 0) {
                    main.postDelayed(() -> call(what, tries - 1, call, then), 1000);
                } else if (reason == WifiP2pManager.P2P_UNSUPPORTED) {
                    setNote("NO WI-FI DIRECT");
                } else if (!"connect".equals(what) || reason != WifiP2pManager.BUSY) {
                    setNote("WI-FI DIRECT ERROR " + reason);
                }
            }
        });
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

    private synchronized void setNote(String text) {
        note = text;
    }

    private synchronized void setAddress(String text) {
        address = text;
    }
}
