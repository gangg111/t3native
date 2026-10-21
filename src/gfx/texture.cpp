#include "gfx/texture.h"

#include <unordered_map>

#include "gfx/gl.h"
#include "platform/log.h"

namespace t3::gfx {

unsigned& gl_generation() {
    static unsigned generation = 1;
    return generation;
}

unsigned& gl_uploads() {
    static unsigned n = 0;
    return n;
}

unsigned& gl_programs() {
    static unsigned n = 0;
    return n;
}

namespace {
unsigned g_stale_binds = 0;
}  // namespace

unsigned Texture::stale_binds() { return g_stale_binds; }

Texture::Texture(const Image& image, bool mipmaps, bool keep) : width_(image.width), height_(image.height), mipmaps_(mipmaps) {
    if (keep) kept_ = image;
    _upload(image);
}

Texture::Texture(int width, int height, std::vector<float> rgba) : width_(width), height_(height), floats_(std::move(rgba)) {
    floats_.resize(static_cast<size_t>(width) * height * 4, 0.0f);
    // Uploaded at the first bind (id()): it may be made where there is no GL (the views' tests).
    generation_ = 0;
}

void Texture::_upload(const Image& image) const {
    gl_uploads() += 1;
    glGenTextures(1, &id_);
    generation_ = gl_generation();
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!floats_.empty()) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width_, height_, 0, GL_RGBA, GL_FLOAT, floats_.data());
        mipmaps_ = false;
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    }
    if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

Texture::Texture(int width, int height) : width_(width), height_(height) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// After a context loss: a file's texture reads its PNG again, a kept image is uploaded again (with
// the mipmaps it had); the old name is forgotten.
bool Texture::_live() const {
    if (generation_ == gl_generation()) return true;
    id_ = 0;
    if (!kept_.empty() || !floats_.empty()) {
        _upload(kept_);
        return true;
    }
    if (!path_.empty()) {
        std::string error;
        Image im = load_png(path_, &error);
        if (!im.empty() && im.width == width_ && im.height == height_) {
            _upload(im);
            return true;
        }
        Log::error("Texture: cannot make " + path_ + " again: " + error);
        generation_ = gl_generation();   // an empty texture from here on (id 0)
        return false;
    }
    return false;
}

unsigned Texture::id() const {
    if (_live()) return id_;
    g_stale_binds += 1;
    if (g_stale_binds == 1) {
        Log::warning("Texture: a " + std::to_string(width_) + "x" + std::to_string(height_) +
                     " texture of a lost GL context was drawn (its view did not make it again)");
    }
    return 0;
}

void Texture::ensure_mipmaps() {
    if (!_live()) return;
    if (mipmaps_ || !floats_.empty()) return;   // a float texture is read with texelFetch only
    glBindTexture(GL_TEXTURE_2D, id_);
    glGenerateMipmap(GL_TEXTURE_2D);
    mipmaps_ = true;
}

Texture::~Texture() {
    if (id_ && generation_ == gl_generation()) glDeleteTextures(1, &id_);
}

void Texture::update(const Image& image) {
    if (!kept_.empty()) kept_ = image;
    if (generation_ != gl_generation()) {
        // A lost context's texture: a new one with these pixels (the old name is forgotten).
        id_ = 0;
        _upload(image);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
}

namespace {
std::unordered_map<std::string, std::weak_ptr<Texture>>& cache() {
    static std::unordered_map<std::string, std::weak_ptr<Texture>> c;
    return c;
}
}  // namespace

TextureRef load_texture(const std::string& path) {
    auto& c = cache();
    auto it = c.find(path);
    if (it != c.end())
        if (TextureRef t = it->second.lock()) return t;
    std::string error;
    Image im = load_png(path, &error);
    if (im.empty()) {
        Log::error("Texture: " + error);
        return nullptr;
    }
    auto t = std::make_shared<Texture>(im);
    t->path_ = path;
    c[path] = t;
    return t;
}

void clear_texture_cache() { cache().clear(); }

}  // namespace t3::gfx
