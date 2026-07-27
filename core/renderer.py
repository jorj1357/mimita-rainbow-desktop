from OpenGL.GL import *
from OpenGL.GL.shaders import compileProgram, compileShader
import numpy as np
import ctypes

from .shaders import vertex_shader_src, fragment_shader_src


class Renderer:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self._program = None
        self._vao = None
        self._vbo = None
        self._ebo = None
        self._capture_tex = None
        self._blend_tex = None
        self._trail_tex = None
        self._trail_fbo = None
        self._blend_frame = None
        self._last_gl_error = 0

        self._init_gl()
        self._build_shaders()
        self._create_textures()

    def _check_gl(self, label):
        err = glGetError()
        if err != GL_NO_ERROR:
            self._last_gl_error = err

    def _init_gl(self):
        glClearColor(0.0, 0.0, 0.0, 0.0)
        self._check_gl("clearColor")
        glDisable(GL_DEPTH_TEST)
        glDisable(GL_BLEND)

        quad = np.array([
            -1.0, -1.0, 0.0, 1.0,
             1.0, -1.0, 1.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
            -1.0,  1.0, 0.0, 0.0,
        ], dtype=np.float32)
        indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)

        self._vao = glGenVertexArrays(1)
        self._vbo = glGenBuffers(1)
        self._ebo = glGenBuffers(1)
        self._check_gl("genBuffers")

        glBindVertexArray(self._vao)
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo)
        glBufferData(GL_ARRAY_BUFFER, quad.nbytes, quad, GL_STATIC_DRAW)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self._ebo)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices, GL_STATIC_DRAW)

        stride = 4 * 4
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, ctypes.c_void_p(0))
        glEnableVertexAttribArray(0)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, ctypes.c_void_p(2 * 4))
        glEnableVertexAttribArray(1)
        glBindVertexArray(0)
        self._check_gl("vao_setup")

    def _build_shaders(self):
        vert = compileShader(vertex_shader_src(), GL_VERTEX_SHADER)
        self._check_gl("compileVert")
        frag = compileShader(fragment_shader_src(), GL_FRAGMENT_SHADER)
        self._check_gl("compileFrag")
        self._program = compileProgram(vert, frag)
        self._check_gl("linkProgram")
        glUseProgram(self._program)
        glUniform1i(glGetUniformLocation(self._program, "u_tex0"), 0)
        glUniform1i(glGetUniformLocation(self._program, "u_tex1"), 1)
        glUseProgram(0)
        self._check_gl("shaderInit")

    def _create_textures(self):
        def make_tex():
            t = glGenTextures(1)
            glBindTexture(GL_TEXTURE_2D, t)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
            return t

        self._capture_tex = make_tex()
        self._blend_tex = make_tex()

        self._trail_tex = make_tex()
        glBindTexture(GL_TEXTURE_2D, self._trail_tex)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, self.width, self.height, 0, GL_RGB, GL_UNSIGNED_BYTE, None)
        self._check_gl("trailTex")

        self._trail_fbo = glGenFramebuffers(1)
        glBindFramebuffer(GL_FRAMEBUFFER, self._trail_fbo)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, self._trail_tex, 0)
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER)
        glBindFramebuffer(GL_FRAMEBUFFER, 0)
        self._check_gl("trailFbo")

    def render_test_pattern(self):
        glViewport(0, 0, self.width, self.height)
        glClear(GL_COLOR_BUFFER_BIT)
        self._check_gl("test_clear")

        glUseProgram(self._program)
        glUniform2f(glGetUniformLocation(self._program, "u_resolution"), self.width, self.height)
        glUniform1f(glGetUniformLocation(self._program, "u_time"), 0.0)

        for name in ["u_hue_enabled", "u_contrast_enabled", "u_saturation_enabled",
                       "u_psychedelic_enabled", "u_blend_enabled", "u_trail_enabled",
                       "u_invert_enabled", "u_grayscale_enabled", "u_pixelate_enabled",
                       "u_glitch_enabled", "u_kaleidoscope_enabled", "u_chromatic_enabled",
                       "u_bloom_enabled", "u_edge_enabled", "u_glow_enabled"]:
            loc = glGetUniformLocation(self._program, name)
            if loc != -1:
                glUniform1i(loc, 0)

        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, self._capture_tex)

        glActiveTexture(GL_TEXTURE1)
        glBindTexture(GL_TEXTURE_2D, self._capture_tex)
        glActiveTexture(GL_TEXTURE0)

        glBindVertexArray(self._vao)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, None)
        glBindVertexArray(0)
        glUseProgram(0)
        self._check_gl("test_draw")

    def upload_test_pattern(self):
        h, w = self.height, self.width
        frame = np.zeros((h, w, 3), dtype=np.uint8)
        frame[:h//3, :, 0] = 255
        frame[h//3:2*h//3, :, 1] = 255
        frame[2*h//3:, :, 2] = 255
        for y in range(0, h, 32):
            for x in range(0, w, 32):
                if (x // 32 + y // 32) % 2 == 0:
                    frame[y:y+16, x:x+16] = [255, 255, 255]

        glBindTexture(GL_TEXTURE_2D, self._capture_tex)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, frame)
        self._check_gl("upload_test")

    def _set_uniforms(self, cfg, time):
        glUseProgram(self._program)

        glUniform2f(glGetUniformLocation(self._program, "u_resolution"), self.width, self.height)
        glUniform1f(glGetUniformLocation(self._program, "u_time"), time)

        e = cfg.get("effects", {})

        hue = e.get("hue", {})
        glUniform1i(glGetUniformLocation(self._program, "u_hue_enabled"), hue.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_hue_amount"), hue.get("amount", 0.0))
        glUniform1f(glGetUniformLocation(self._program, "u_hue_speed"), hue.get("speed", 0.0))

        cn = e.get("contrast", {})
        glUniform1i(glGetUniformLocation(self._program, "u_contrast_enabled"), cn.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_contrast_amount"), cn.get("amount", 1.0))

        sat = e.get("saturation", {})
        glUniform1i(glGetUniformLocation(self._program, "u_saturation_enabled"), sat.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_saturation_amount"), sat.get("amount", 1.0))

        psy = e.get("psychedelic", {})
        glUniform1i(glGetUniformLocation(self._program, "u_psychedelic_enabled"), psy.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_psy_speed"), psy.get("speed", 1.0))
        glUniform1f(glGetUniformLocation(self._program, "u_psy_intensity"), psy.get("intensity", 1.0))

        bl = e.get("blend_mode", {})
        glUniform1i(glGetUniformLocation(self._program, "u_blend_enabled"), bl.get("enabled", False))
        mode_map = {"additive": 0, "xnor": 1, "subtract": 2, "multiply": 3, "screen": 4, "difference": 5, "overlay": 6}
        glUniform1i(glGetUniformLocation(self._program, "u_blend_mode"), mode_map.get(bl.get("mode", "additive"), 0))

        tr = e.get("motion_trail", {})
        glUniform1i(glGetUniformLocation(self._program, "u_trail_enabled"), tr.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_trail_decay"), tr.get("decay", 0.85))

        glUniform1i(glGetUniformLocation(self._program, "u_invert_enabled"), e.get("invert", {}).get("enabled", False))
        glUniform1i(glGetUniformLocation(self._program, "u_grayscale_enabled"), e.get("grayscale", {}).get("enabled", False))

        px = e.get("pixelate", {})
        glUniform1i(glGetUniformLocation(self._program, "u_pixelate_enabled"), px.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_pixelate_block"), px.get("block_size", 8))

        gl2 = e.get("glitch", {})
        glUniform1i(glGetUniformLocation(self._program, "u_glitch_enabled"), gl2.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_glitch_intensity"), gl2.get("intensity", 0.05))

        kl = e.get("kaleidoscope", {})
        glUniform1i(glGetUniformLocation(self._program, "u_kaleidoscope_enabled"), kl.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_kaleidoscope_segments"), kl.get("segments", 8))

        ch = e.get("chromatic_aberration", {})
        glUniform1i(glGetUniformLocation(self._program, "u_chromatic_enabled"), ch.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_chromatic_amount"), ch.get("amount", 0.003))

        bm = e.get("bloom", {})
        glUniform1i(glGetUniformLocation(self._program, "u_bloom_enabled"), bm.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_bloom_threshold"), bm.get("threshold", 0.8))
        glUniform1f(glGetUniformLocation(self._program, "u_bloom_intensity"), bm.get("intensity", 0.3))

        ed = e.get("edge_detect", {})
        glUniform1i(glGetUniformLocation(self._program, "u_edge_enabled"), ed.get("enabled", False))

        gw = e.get("glow", {})
        glUniform1i(glGetUniformLocation(self._program, "u_glow_enabled"), gw.get("enabled", False))
        glUniform1f(glGetUniformLocation(self._program, "u_glow_intensity"), gw.get("intensity", 0.3))
        glUniform1f(glGetUniformLocation(self._program, "u_glow_speed"), gw.get("speed", 0.3))
        glUniform1f(glGetUniformLocation(self._program, "u_glow_distance"), gw.get("distance", 0.3))
        glUniform1i(glGetUniformLocation(self._program, "u_glow_move_enabled"), gw.get("move_enabled", True))

        self._check_gl("uniforms")

    def upload_frame(self, frame):
        glBindTexture(GL_TEXTURE_2D, self._capture_tex)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, frame.shape[1], frame.shape[0], 0, GL_RGB, GL_UNSIGNED_BYTE, frame)
        self._check_gl("upload_frame")

    def render(self, cfg, time):
        glViewport(0, 0, self.width, self.height)
        glClear(GL_COLOR_BUFFER_BIT)
        self._check_gl("render_clear")

        e = cfg.get("effects", {})
        trail_enabled = e.get("motion_trail", {}).get("enabled", False)
        blend_enabled = e.get("blend_mode", {}).get("enabled", False)

        self._set_uniforms(cfg, time)

        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, self._capture_tex)
        self._check_gl("bind_tex0")

        glActiveTexture(GL_TEXTURE1)
        if trail_enabled:
            glBindTexture(GL_TEXTURE_2D, self._trail_tex)
        elif blend_enabled and self._blend_frame is not None:
            glBindTexture(GL_TEXTURE_2D, self._blend_tex)
        else:
            glBindTexture(GL_TEXTURE_2D, self._capture_tex)
        glActiveTexture(GL_TEXTURE0)
        self._check_gl("bind_tex1")

        glBindVertexArray(self._vao)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, None)
        glBindVertexArray(0)
        self._check_gl("draw")

        if trail_enabled:
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0)
            glReadBuffer(GL_BACK)
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self._trail_fbo)
            glBlitFramebuffer(0, 0, self.width, self.height, 0, 0, self.width, self.height, GL_COLOR_BUFFER_BIT, GL_NEAREST)
            glBindFramebuffer(GL_FRAMEBUFFER, 0)
            self._check_gl("trail_blit")

        if blend_enabled and e.get("blend_mode", {}).get("freeze_frame", False):
            self.freeze_blend_frame()

    def destroy(self):
        if self._program:
            glDeleteProgram(self._program)
        for t in [self._capture_tex, self._blend_tex, self._trail_tex]:
            if t:
                glDeleteTextures(1, [t])
        if self._trail_fbo:
            glDeleteFramebuffers(1, [self._trail_fbo])
        if self._vao:
            glDeleteVertexArrays(1, [self._vao])
            glDeleteBuffers(1, [self._vbo])
            glDeleteBuffers(1, [self._ebo])
