#include "gfx/render_target.h"

#include "gfx/gl.h"
#include "platform/log.h"

namespace t3::gfx {

RenderTarget::RenderTarget(int width, int height) {
    texture_ = std::make_shared<Texture>(width > 0 ? width : 1, height > 0 ? height : 1);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_->id(), 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Log::error("gfx: render target " + std::to_string(width) + "x" + std::to_string(height) + " is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderTarget::~RenderTarget() {
    if (fbo_ && generation_ == gl_generation()) glDeleteFramebuffers(1, &fbo_);
}

void RenderTarget::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width(), height());
}

void RenderTarget::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

}  // namespace t3::gfx
