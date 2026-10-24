#include "rack_textures.h"
#include "../theme/theme.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ui::rack_skin {

namespace {

constexpr uint32_t kChassisW = 64;
constexpr uint32_t kChassisH = 32;
uint8_t g_chassisBuf[kChassisW * kChassisH * 4];
lv_image_dsc_t g_chassisDsc{};

constexpr uint32_t kFaceplateW = 64;
constexpr uint32_t kFaceplateH = 32;
uint8_t g_faceplateBuf[kFaceplateW * kFaceplateH * 4];
lv_image_dsc_t g_faceplateDsc{};

constexpr uint32_t kGreyFaceplateW = 64;
constexpr uint32_t kGreyFaceplateH = 32;
uint8_t g_greyFaceplateBuf[kGreyFaceplateW * kGreyFaceplateH * 4];
lv_image_dsc_t g_greyFaceplateDsc{};

constexpr uint32_t kBayW = 32;
constexpr uint32_t kBayH = 32;
uint8_t g_bayBuf[kBayW * kBayH * 4];
lv_image_dsc_t g_bayDsc{};

constexpr uint32_t kLcdMaskW = 8;
constexpr uint32_t kLcdMaskH = 8;
uint8_t g_lcdMaskBuf[kLcdMaskW * kLcdMaskH * 4];
lv_image_dsc_t g_lcdMaskDsc{};

constexpr uint32_t kScrewW = 14;
constexpr uint32_t kScrewH = 14;
uint8_t g_screwBuf[kScrewW * kScrewH * 4];
lv_image_dsc_t g_screwDsc{};

bool g_initialized = false;

inline uint8_t clamp_u8(int v) {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

inline void put_xrgb(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b) {
    // Little-endian BGRA / XRGB8888 memory layout for LVGL 9.4
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = 0xFF;
}

inline void put_argb(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = a;
}

uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

void generate_brushed_tile(uint8_t* buf, uint32_t w, uint32_t h,
                           int baseR, int baseG, int baseB,
                           int streakAmp, int microGrainAmp, uint32_t seed) {
    for (uint32_t y = 0; y < h; ++y) {
        // Row-dominant horizontal brushed aluminum grain
        const int rowHash = static_cast<int>(hash_u32(y * 97u + seed) & 0xFF) - 128;
        const int rowDelta = (rowHash * streakAmp) / 128;
        // Subtle vertical sheen modulation (periodic over tile height)
        const double vWave = std::sin(2.0 * 3.14159265358979323846 * static_cast<double>(y) / static_cast<double>(h));
        const int sheen = static_cast<int>(std::round(vWave * 2.0));

        for (uint32_t x = 0; x < w; ++x) {
            // Seamless horizontal micro-streak using periodic harmonics over tile width
            const double ang1 = 2.0 * 3.14159265358979323846 * static_cast<double>(x) / static_cast<double>(w);
            const double phase1 = static_cast<double>(hash_u32(y * 31u + seed) & 0xFF) * 0.02454369;
            const double phase2 = static_cast<double>(hash_u32(y * 73u + seed + 19u) & 0xFF) * 0.02454369;
            const double streak = 0.65 * std::sin(ang1 + phase1) + 0.35 * std::sin(2.0 * ang1 + phase2);
            const int micro = static_cast<int>(std::round(streak * microGrainAmp));

            const int d = rowDelta + sheen + micro;
            const uint8_t r = clamp_u8(baseR + d);
            const uint8_t g = clamp_u8(baseG + d);
            const uint8_t b = clamp_u8(baseB + d);
            put_xrgb(&buf[(y * w + x) * 4], r, g, b);
        }
    }
}

void generate_recessed_bay_tile() {
    for (uint32_t y = 0; y < kBayH; ++y) {
        for (uint32_t x = 0; x < kBayW; ++x) {
            // Subtle machined micro-grid / matte dark acrylic texture in deep near-black (#111210 base)
            const int checker = ((x + y) & 1) ? 1 : -1;
            const int scanline = (y % 4 == 0) ? -1 : 0;
            const int baseR = 17 + checker + scanline;
            const int baseG = 18 + checker + scanline;
            const int baseB = 16 + checker + scanline;
            put_xrgb(&g_bayBuf[(y * kBayW + x) * 4],
                     clamp_u8(baseR), clamp_u8(baseG), clamp_u8(baseB));
        }
    }
}

void generate_lcd_dot_matrix_tile() {
    for (uint32_t y = 0; y < kLcdMaskH; ++y) {
        for (uint32_t x = 0; x < kLcdMaskW; ++x) {
            uint8_t* px = &g_lcdMaskBuf[(y * kLcdMaskW + x) * 4];
            const bool is_h_line = (y % 2 == 1);
            const bool is_v_line = (x % 2 == 1);

            if (!is_h_line && !is_v_line) {
                // Active phosphor/crystal dot cell: completely transparent
                put_argb(px, 0, 0, 0, 0);
            } else if (is_h_line && is_v_line) {
                // Cross-junction intersection: darkest neutral interstice (~18% opacity dark glass)
                put_argb(px, 12, 12, 12, 46);
            } else {
                // Micro-gap line (scanline / matrix column boundary): ~11% opacity neutral grey glass
                put_argb(px, 14, 14, 14, 28);
            }
        }
    }
}

void generate_rack_screw_sprite() {
    const float cx = 6.5f;
    const float cy = 6.5f;
    for (uint32_t y = 0; y < kScrewH; ++y) {
        for (uint32_t x = 0; x < kScrewW; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            uint8_t* px = &g_screwBuf[(y * kScrewW + x) * 4];

            if (dist > 6.6f) {
                put_argb(px, 0, 0, 0, 0);
                continue;
            }
            // Outer counter-bore shadow ring with neutral dark steel lip highlight
            if (dist > 5.4f) {
                const uint8_t a = (dist > 6.0f)
                    ? static_cast<uint8_t>(std::clamp((6.6f - dist) / 0.6f * 220.0f, 0.0f, 220.0f))
                    : 235;
                const int rimLight = (dx + dy > 0.0f) ? 38 : 12;
                put_argb(px, clamp_u8(rimLight + 1), clamp_u8(rimLight + 1), clamp_u8(rimLight), a);
                continue;
            }
            // Inner hex/Phillips socket cavity
            const bool inCrossH = (std::abs(dy) < 1.15f && std::abs(dx) < 3.4f);
            const bool inCrossV = (std::abs(dx) < 1.15f && std::abs(dy) < 3.4f);
            const bool inCenterPit = (dist < 1.8f);
            if (inCrossH || inCrossV || inCenterPit) {
                const int pit = (dy > 0.4f) ? 24 : 11;
                put_argb(px, clamp_u8(pit), clamp_u8(pit), clamp_u8(pit), 255);
                continue;
            }
            // Machined steel screw dome in neutral dark studio steel (no brown/gold cast)
            const float angleLight = (-dx * 0.7f - dy * 0.7f) / 5.4f;
            const float radialSheen = std::cos(std::atan2(dy, dx) * 2.0f + 0.8f) * 7.0f;
            const int shade = static_cast<int>(std::round(60.0f + angleLight * 26.0f + radialSheen));
            const int bevel = (dist > 4.3f) ? ((dy < 0.0f) ? 16 : -14) : 0;
            const int v = shade + bevel;
            put_argb(px, clamp_u8(v), clamp_u8(v), clamp_u8(v), 255);
        }
    }
}

} // namespace

void init_textures() {
    if (g_initialized) return;

    // 1. Dark Neutral Metal Chassis Tile (#242522 base, neutral greyscale micro-grain)
    generate_brushed_tile(g_chassisBuf, kChassisW, kChassisH, 36, 37, 34, 2, 1, 0x1A2B3C4Du);
    g_chassisDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_chassisDsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_chassisDsc.header.flags = 0;
    g_chassisDsc.header.w = kChassisW;
    g_chassisDsc.header.h = kChassisH;
    g_chassisDsc.header.stride = kChassisW * 4;
    g_chassisDsc.data_size = sizeof(g_chassisBuf);
    g_chassisDsc.data = g_chassisBuf;

    // 2. Neutral Panel Surface Tile (#242522 base, neutral greyscale micro-grain)
    generate_brushed_tile(g_faceplateBuf, kFaceplateW, kFaceplateH, 36, 37, 34, 2, 1, 0x5E6F7A8Bu);
    g_faceplateDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_faceplateDsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_faceplateDsc.header.flags = 0;
    g_faceplateDsc.header.w = kFaceplateW;
    g_faceplateDsc.header.h = kFaceplateH;
    g_faceplateDsc.header.stride = kFaceplateW * 4;
    g_faceplateDsc.data_size = sizeof(g_faceplateBuf);
    g_faceplateDsc.data = g_faceplateBuf;

    // 3. Recessed Dark Acrylic LCD Display Bay Tile (#111210 base)
    generate_recessed_bay_tile();
    g_bayDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_bayDsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_bayDsc.header.flags = 0;
    g_bayDsc.header.w = kBayW;
    g_bayDsc.header.h = kBayH;
    g_bayDsc.header.stride = kBayW * 4;
    g_bayDsc.data_size = sizeof(g_bayBuf);
    g_bayDsc.data = g_bayBuf;

    // 4. Physical LCD Dot-Matrix Mask Tile (8x8 ARGB8888, 2x2 dot pitch)
    generate_lcd_dot_matrix_tile();
    g_lcdMaskDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_lcdMaskDsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_lcdMaskDsc.header.flags = 0;
    g_lcdMaskDsc.header.w = kLcdMaskW;
    g_lcdMaskDsc.header.h = kLcdMaskH;
    g_lcdMaskDsc.header.stride = kLcdMaskW * 4;
    g_lcdMaskDsc.data_size = sizeof(g_lcdMaskBuf);
    g_lcdMaskDsc.data = g_lcdMaskBuf;

    // 5. Recessed Hardware Rack Screw Sprite (14x14 ARGB8888)
    generate_rack_screw_sprite();
    g_screwDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_screwDsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_screwDsc.header.flags = 0;
    g_screwDsc.header.w = kScrewW;
    g_screwDsc.header.h = kScrewH;
    g_screwDsc.header.stride = kScrewW * 4;
    g_screwDsc.data_size = sizeof(g_screwBuf);
    g_screwDsc.data = g_screwBuf;

    // 6. Vintage Grey Brushed Aluminum Faceplate Tile (#5C6774 base - preserved for vintage compressor)
    generate_brushed_tile(g_greyFaceplateBuf, kGreyFaceplateW, kGreyFaceplateH, 92, 103, 116, 7, 3, 0x8C9EA1B2u);
    g_greyFaceplateDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_greyFaceplateDsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_greyFaceplateDsc.header.flags = 0;
    g_greyFaceplateDsc.header.w = kGreyFaceplateW;
    g_greyFaceplateDsc.header.h = kGreyFaceplateH;
    g_greyFaceplateDsc.header.stride = kGreyFaceplateW * 4;
    g_greyFaceplateDsc.data_size = sizeof(g_greyFaceplateBuf);
    g_greyFaceplateDsc.data = g_greyFaceplateBuf;

    g_initialized = true;
}

void regenerate_textures_for_theme() {
    init_textures();
    uint32_t panel_hex = theme_get_color_hex(ThemeColorRole::Panel);
    const uint8_t pr = static_cast<uint8_t>((panel_hex >> 16) & 0xFF);
    const uint8_t pg = static_cast<uint8_t>((panel_hex >> 8) & 0xFF);
    const uint8_t pb = static_cast<uint8_t>(panel_hex & 0xFF);
    generate_brushed_tile(g_chassisBuf, kChassisW, kChassisH, pr, pg, pb, 4, 2, 0x8A9E1122u);
    generate_brushed_tile(g_faceplateBuf, kFaceplateW, kFaceplateH, pr, pg, pb, 6, 2, 0xA5C31024u);

    uint32_t rec_hex = theme_get_color_hex(ThemeColorRole::RecessedSurface);
    const int rr = static_cast<int>((rec_hex >> 16) & 0xFF);
    const int rg = static_cast<int>((rec_hex >> 8) & 0xFF);
    const int rb = static_cast<int>(rec_hex & 0xFF);
    for (uint32_t y = 0; y < kBayH; ++y) {
        for (uint32_t x = 0; x < kBayW; ++x) {
            const int checker = ((x + y) & 1) ? 1 : -1;
            const int scanline = (y % 4 == 0) ? -1 : 0;
            put_xrgb(&g_bayBuf[(y * kBayW + x) * 4],
                     clamp_u8(rr + checker + scanline),
                     clamp_u8(rg + checker + scanline),
                     clamp_u8(rb + checker + scanline));
        }
    }
}

const lv_image_dsc_t* chassis_brushed_tile() {
    init_textures();
    return &g_chassisDsc;
}

const lv_image_dsc_t* faceplate_brushed_tile() {
    init_textures();
    return &g_faceplateDsc;
}

const lv_image_dsc_t* grey_faceplate_brushed_tile() {
    init_textures();
    return &g_greyFaceplateDsc;
}

const lv_image_dsc_t* recessed_bay_tile() {
    init_textures();
    return &g_bayDsc;
}

const lv_image_dsc_t* lcd_dot_matrix_tile() {
    init_textures();
    return &g_lcdMaskDsc;
}

const lv_image_dsc_t* rack_screw_icon() {
    init_textures();
    return &g_screwDsc;
}

void draw_lcd_dot_matrix_mask(lv_layer_t* layer, const lv_area_t* coords) {
    if (!layer || !coords) return;
    init_textures();

    // 1. Physical dot-matrix liquid crystal / phosphor grid overlay
    lv_draw_image_dsc_t mask_dsc;
    lv_draw_image_dsc_init(&mask_dsc);
    mask_dsc.src = &g_lcdMaskDsc;
    mask_dsc.tile = 1;
    mask_dsc.image_area = *coords;
    mask_dsc.clip_radius = 0;
    lv_draw_image(layer, &mask_dsc, coords);

    // 2. Physical recessed aperture edge shading
    lv_draw_line_dsc_t edge;
    lv_draw_line_dsc_init(&edge);
    edge.width = 1;

    // Top inner drop shadow
    edge.color = lv_color_hex(0x070806);
    edge.p1.x = coords->x1 + 2; edge.p1.y = coords->y1 + 1;
    edge.p2.x = coords->x2 - 2; edge.p2.y = coords->y1 + 1;
    lv_draw_line(layer, &edge);

    // Left inner drop shadow
    edge.p1.x = coords->x1 + 1; edge.p1.y = coords->y1 + 2;
    edge.p2.x = coords->x1 + 1; edge.p2.y = coords->y2 - 2;
    lv_draw_line(layer, &edge);

    // Bottom subtle metallic lip highlight
    edge.color = COLOR_SUBTLE_SEPARATOR;
    edge.p1.x = coords->x1 + 2; edge.p1.y = coords->y2 - 1;
    edge.p2.x = coords->x2 - 2; edge.p2.y = coords->y2 - 1;
    lv_draw_line(layer, &edge);

    // Right subtle metallic lip highlight
    edge.p1.x = coords->x2 - 1; edge.p1.y = coords->y1 + 2;
    edge.p2.x = coords->x2 - 1; edge.p2.y = coords->y2 - 2;
    lv_draw_line(layer, &edge);
}

void apply_chassis_panel(lv_obj_t* obj) {
    if (!obj) return;
    apply_chassis_panel(obj, COLOR_STANDARD_BORDER);
}

void apply_chassis_panel(lv_obj_t* obj, lv_color_t border_color) {
    if (!obj) return;
    init_textures();
    lv_obj_set_style_bg_color(obj, COLOR_CHASSIS, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(obj, &g_chassisDsc, 0);
    lv_obj_set_style_bg_image_tiled(obj, true, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, COLOR_RECESSED, 0);
    lv_obj_set_style_radius(obj, 3, 0);
}

void apply_faceplate_panel(lv_obj_t* obj, bool add_corner_screws) {
    if (!obj) return;
    apply_faceplate_panel(obj, COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    if (add_corner_screws) {
        add_faceplate_corner_screws(obj, 5, 5);
    }
}

void apply_faceplate_panel(lv_obj_t* obj, lv_color_t border_color, lv_color_t bg_tint) {
    if (!obj) return;
    init_textures();
    lv_obj_set_style_bg_color(obj, bg_tint, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(obj, &g_faceplateDsc, 0);
    lv_obj_set_style_bg_image_tiled(obj, true, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_70, 0);
    // Machined metallic bevel border + dark chassis seam outline
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, COLOR_RECESSED, 0);
    lv_obj_set_style_shadow_width(obj, 6, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 2, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x0C0C0B), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_radius(obj, 3, 0);
}

void apply_grey_faceplate_panel(lv_obj_t* obj, bool add_corner_screws) {
    if (!obj) return;
    apply_grey_faceplate_panel(obj, lv_color_hex(0x788698), lv_color_hex(0x56606C));
    if (add_corner_screws) {
        add_faceplate_corner_screws(obj, 5, 5);
    }
}

void apply_grey_faceplate_panel(lv_obj_t* obj, lv_color_t border_color, lv_color_t bg_tint) {
    if (!obj) return;
    init_textures();
    lv_obj_set_style_bg_color(obj, bg_tint, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(obj, &g_greyFaceplateDsc, 0);
    lv_obj_set_style_bg_image_tiled(obj, true, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_70, 0);
    // Machined metallic bevel border + dark chassis seam outline
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(0x1B2129), 0);
    lv_obj_set_style_shadow_width(obj, 6, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 2, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_radius(obj, 3, 0);
}

void apply_channel_strip_panel(lv_obj_t* obj, bool is_selected) {
    if (!obj) return;
    init_textures();
    const lv_color_t bg_tint = is_selected ? COLOR_RAISED_PANEL : COLOR_PANEL_SURFACE;
    const lv_color_t border = is_selected ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER;
    lv_obj_set_style_bg_color(obj, bg_tint, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(obj, &g_faceplateDsc, 0);
    lv_obj_set_style_bg_image_tiled(obj, true, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_60, 0);
    lv_obj_set_style_border_width(obj, is_selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, COLOR_RECESSED, 0);
    lv_obj_set_style_shadow_width(obj, is_selected ? 8 : 4, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 2, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x080807), 0);
    lv_obj_set_style_shadow_opa(obj, is_selected ? LV_OPA_70 : LV_OPA_40, 0);
    lv_obj_set_style_radius(obj, 2, 0);
}

void apply_recessed_display(lv_obj_t* obj, lv_color_t accent_border) {
    if (!obj) return;
    apply_recessed_display(obj, COLOR_LCD_BG, accent_border);
}

void apply_recessed_display(lv_obj_t* obj, lv_color_t bg_color, lv_color_t accent_border) {
    if (!obj) return;
    init_textures();
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(obj, &g_bayDsc, 0);
    lv_obj_set_style_bg_image_tiled(obj, true, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_60, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, accent_border, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, COLOR_RECESSED, 0);
    lv_obj_set_style_radius(obj, 2, 0);
}

void apply_hardware_button(lv_obj_t* btn, lv_color_t body_color, lv_color_t border_color, lv_color_t text_color) {
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, body_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_darken(body_color, 24), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, border_color, 0);
    lv_obj_set_style_outline_width(btn, 1, 0);
    lv_obj_set_style_outline_color(btn, COLOR_RECESSED, 0);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_set_style_text_color(btn, text_color, 0);
    lv_obj_set_style_border_color(btn, COLOR_DISABLED, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, COLOR_DISABLED, LV_STATE_DISABLED);
}

void apply_hardware_button(lv_obj_t* btn, bool active, lv_color_t active_border, lv_color_t active_bg, lv_color_t inactive_bg) {
    if (!btn) return;
    const lv_color_t body = active ? active_bg : lv_color_lighten(inactive_bg, 8);
    const lv_color_t grad = active ? lv_color_darken(active_bg, 18) : lv_color_darken(inactive_bg, 12);
    lv_obj_set_style_bg_color(btn, body, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(btn, grad, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, active ? active_border : COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_outline_width(btn, 1, 0);
    lv_obj_set_style_outline_color(btn, COLOR_RECESSED, 0);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_DISABLED, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, COLOR_DISABLED, LV_STATE_DISABLED);
}

lv_obj_t* create_rack_screw(lv_obj_t* parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs) {
    if (!parent) return nullptr;
    init_textures();
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, &g_screwDsc);
    lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(img, align, x_ofs, y_ofs);
    return img;
}

lv_obj_t* create_rack_screw(lv_obj_t* parent, int32_t x, int32_t y) {
    if (!parent) return nullptr;
    init_textures();
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, &g_screwDsc);
    lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(img, x, y);
    return img;
}

void add_faceplate_corner_screws(lv_obj_t* faceplate, int32_t inset_x, int32_t inset_y) {
    if (!faceplate) return;
    create_rack_screw(faceplate, LV_ALIGN_TOP_LEFT, inset_x, inset_y);
    create_rack_screw(faceplate, LV_ALIGN_TOP_RIGHT, -inset_x, inset_y);
    create_rack_screw(faceplate, LV_ALIGN_BOTTOM_LEFT, inset_x, -inset_y);
    create_rack_screw(faceplate, LV_ALIGN_BOTTOM_RIGHT, -inset_x, -inset_y);
}

void add_faceplate_corner_screws(lv_obj_t* faceplate, int32_t w, int32_t h, int32_t inset) {
    if (!faceplate) return;
    create_rack_screw(faceplate, inset, inset);
    create_rack_screw(faceplate, w - 14 - inset, inset);
    create_rack_screw(faceplate, inset, h - 14 - inset);
    create_rack_screw(faceplate, w - 14 - inset, h - 14 - inset);
}

lv_obj_t* create_bezel_led(lv_obj_t* parent, lv_color_t color, bool active) {
    if (!parent) return nullptr;
    lv_obj_t* led = lv_obj_create(parent);
    lv_obj_set_size(led, 11, 11);
    lv_obj_set_style_pad_all(led, 0, 0);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(led, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(led, LV_OBJ_FLAG_CLICKABLE);
    set_bezel_led_state(led, color, active);
    return led;
}

lv_obj_t* create_bezel_led(lv_obj_t* parent, int32_t x, int32_t y, lv_color_t color, bool active) {
    if (!parent) return nullptr;
    lv_obj_t* led = create_bezel_led(parent, color, active);
    lv_obj_set_pos(led, x, y);
    return led;
}

void set_bezel_led_state(lv_obj_t* led, lv_color_t color, bool active) {
    if (!led) return;
    const lv_color_t fill = active ? color : lv_color_darken(color, 185);
    lv_obj_set_style_bg_color(led, fill, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    // Machined dark-metal bezel ring around the LED lens
    lv_obj_set_style_border_width(led, 2, 0);
    lv_obj_set_style_border_color(led, active ? lv_color_lighten(color, 70) : lv_color_hex(0x2A323D), 0);
    lv_obj_set_style_outline_width(led, 1, 0);
    lv_obj_set_style_outline_color(led, lv_color_hex(0x090C10), 0);
    lv_obj_set_style_shadow_width(led, active ? 6 : 0, 0);
    lv_obj_set_style_shadow_color(led, color, 0);
    lv_obj_set_style_shadow_opa(led, active ? LV_OPA_60 : LV_OPA_0, 0);
}

} // namespace ui::rack_skin
