VERTEX_SHADER_SRC = """
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
"""

FRAGMENT_SHADER_TEMPLATE = """
#version 330 core
in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_tex0;
uniform sampler2D u_tex1;

uniform float u_time;
uniform vec2 u_resolution;

uniform bool u_hue_enabled;
uniform float u_hue_amount;
uniform float u_hue_speed;

uniform bool u_contrast_enabled;
uniform float u_contrast_amount;

uniform bool u_saturation_enabled;
uniform float u_saturation_amount;

uniform bool u_psychedelic_enabled;
uniform float u_psy_speed;
uniform float u_psy_intensity;

uniform bool u_blend_enabled;
uniform int u_blend_mode;

uniform bool u_trail_enabled;
uniform float u_trail_decay;

uniform bool u_invert_enabled;

uniform bool u_grayscale_enabled;

uniform bool u_pixelate_enabled;
uniform float u_pixelate_block;

uniform bool u_glitch_enabled;
uniform float u_glitch_intensity;

uniform bool u_kaleidoscope_enabled;
uniform float u_kaleidoscope_segments;

uniform bool u_chromatic_enabled;
uniform float u_chromatic_amount;

uniform bool u_bloom_enabled;
uniform float u_bloom_threshold;
uniform float u_bloom_intensity;

uniform bool u_edge_enabled;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = v_uv;
    vec3 color = texture(u_tex0, uv).rgb;

    // --- invert ---
    if (u_invert_enabled) {
        color = 1.0 - color;
    }

    // --- grayscale ---
    if (u_grayscale_enabled) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        color = vec3(gray);
    }

    // --- hue ---
    if (u_hue_enabled) {
        vec3 hsv = rgb2hsv(color);
        float shift = u_hue_amount + (u_hue_speed * u_time * 0.1);
        hsv.x = fract(hsv.x + shift);
        color = hsv2rgb(hsv);
    }

    // --- contrast ---
    if (u_contrast_enabled) {
        color = vec3(0.5) + (color - vec3(0.5)) * u_contrast_amount;
    }

    // --- saturation ---
    if (u_saturation_enabled) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(vec3(gray), color, u_saturation_amount);
    }

    // --- psychedelic ---
    if (u_psychedelic_enabled) {
        float t = u_time * u_psy_speed;
        vec3 hsv = rgb2hsv(color);
        hsv.x = fract(hsv.x + t * 0.05);
        hsv.y *= 0.5 + 0.5 * sin(t * 0.1);
        hsv.z *= 0.5 + 0.5 * sin(t * 0.07 + 1.0);
        color = hsv2rgb(hsv);
        color = vec3(0.5) + (color - vec3(0.5)) * (1.0 + 0.5 * sin(t * 0.03));
    }

    // --- pixelate ---
    if (u_pixelate_enabled) {
        vec2 d = u_pixelate_block / u_resolution;
        vec2 coord = floor(uv / d) * d + d * 0.5;
        color = texture(u_tex0, coord).rgb;
    }

    // --- kaleidoscope ---
    if (u_kaleidoscope_enabled) {
        vec2 p = uv - 0.5;
        float r = length(p);
        float a = atan(p.y, p.x);
        float seg = u_kaleidoscope_segments;
        a = mod(a, 6.2832 / seg);
        a = abs(a - 3.1416 / seg);
        vec2 kp = vec2(cos(a), sin(a)) * r + 0.5;
        color = texture(u_tex0, kp).rgb;
    }

    // --- chromatic aberration ---
    if (u_chromatic_enabled) {
        float a = u_chromatic_amount;
        float r = texture(u_tex0, uv + vec2(a, 0.0)).r;
        float g = texture(u_tex0, uv).g;
        float b = texture(u_tex0, uv - vec2(a, 0.0)).b;
        color = vec3(r, g, b);
    }

    // --- glitch ---
    if (u_glitch_enabled) {
        float row = floor(uv.y * u_resolution.y);
        float glitch_line = sin(row * 3.14159 * 0.1 + u_time * 10.0) * 0.5 + 0.5;
        if (glitch_line > 1.0 - u_glitch_intensity) {
            float offset = (sin(row * 7.0 + u_time * 15.0) * 0.5 + 0.5) * 0.1;
            vec2 guv = uv + vec2(offset, 0.0);
            color = texture(u_tex0, guv).rgb;
        }
        float block = sin(row * 13.0 + u_time * 8.0) * 0.5 + 0.5;
        if (block > 0.95) {
            color = vec3(sin(u_time * 20.0) * 0.5 + 0.5, 0.0, 0.0);
        }
    }

    // --- blend mode (with u_tex1) ---
    if (u_blend_enabled) {
        vec3 bcolor = texture(u_tex1, uv).rgb;
        if (u_blend_mode == 0) { // additive
            color = min(color + bcolor, 1.0);
        } else if (u_blend_mode == 1) { // xnor
            color = 1.0 - abs(color * 2.0 - 1.0 - (bcolor * 2.0 - 1.0)) * 0.5;
        } else if (u_blend_mode == 2) { // subtract
            color = max(color - bcolor, 0.0);
        } else if (u_blend_mode == 3) { // multiply
            color = color * bcolor;
        } else if (u_blend_mode == 4) { // screen
            color = 1.0 - (1.0 - color) * (1.0 - bcolor);
        } else if (u_blend_mode == 5) { // difference
            color = abs(color - bcolor);
        } else if (u_blend_mode == 6) { // overlay
            vec3 base = bcolor;
            vec3 blend = color;
            color = mix(2.0 * base * blend, 1.0 - 2.0 * (1.0 - base) * (1.0 - blend), step(0.5, base));
        }
    }

    // --- edge detect ---
    if (u_edge_enabled) {
        vec2 px = 1.0 / u_resolution;
        vec3 tl = texture(u_tex0, uv + vec2(-px.x, -px.y)).rgb;
        vec3 tr = texture(u_tex0, uv + vec2(px.x, -px.y)).rgb;
        vec3 bl = texture(u_tex0, uv + vec2(-px.x, px.y)).rgb;
        vec3 br = texture(u_tex0, uv + vec2(px.x, px.y)).rgb;
        vec3 t = texture(u_tex0, uv + vec2(0.0, -px.y)).rgb;
        vec3 b = texture(u_tex0, uv + vec2(0.0, px.y)).rgb;
        vec3 l = texture(u_tex0, uv + vec2(-px.x, 0.0)).rgb;
        vec3 r = texture(u_tex0, uv + vec2(px.x, 0.0)).rgb;
        vec3 gx = tl + 2.0 * l + bl - tr - 2.0 * r - br;
        vec3 gy = tl + 2.0 * t + tr - bl - 2.0 * b - br;
        color = sqrt(gx * gx + gy * gy);
    }

    // --- bloom ---
    if (u_bloom_enabled) {
        vec3 bright = max(color - vec3(u_bloom_threshold), 0.0);
        vec2 px = 1.0 / u_resolution;
        vec3 blur = vec3(0.0);
        float kernel[9] = float[](1.0, 2.0, 1.0, 2.0, 4.0, 2.0, 1.0, 2.0, 1.0);
        float ksum = 16.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vec2 o = vec2(float(x), float(y)) * px * 2.0;
                vec3 s = max(texture(u_tex0, uv + o).rgb - vec3(u_bloom_threshold), 0.0);
                blur += s * kernel[(y+1)*3 + (x+1)];
            }
        }
        blur /= ksum;
        color = color + blur * u_bloom_intensity;
    }

    // --- motion trail ---
    if (u_trail_enabled) {
        vec3 prev = texture(u_tex1, uv).rgb;
        color = mix(prev, color, 1.0 - u_trail_decay);
    }

    frag_color = vec4(color, 1.0);
}
"""


def vertex_shader_src():
    return VERTEX_SHADER_SRC


def fragment_shader_src():
    return FRAGMENT_SHADER_TEMPLATE
