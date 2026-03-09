/*
 *  media_screen.c – Media / music control screen
 *
 *  Shows: track title, artist, album, play state.
 *  Controls: previous, play/pause, next, volume +/-.
 *  Sends commands to phone via gb_send_music_cmd().
 */

#include "media_screen.h"
#include <lvgl.h>
#include <string.h>
#include <stdio.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG        lv_color_make(10,  10,  15)
#define COL_TITLE     lv_color_make(100, 100, 110)
#define COL_TRACK     lv_color_make(255, 255, 255)
#define COL_ARTIST    lv_color_make(180, 180, 200)
#define COL_ALBUM     lv_color_make(120, 120, 140)
#define COL_BTN_BG    lv_color_make(40,  42,  50)
#define COL_BTN_PR    lv_color_make(65,  70,  85)
#define COL_ICON      lv_color_make(255, 255, 255)
#define COL_PLAY      lv_color_make(30,  200, 100)
#define COL_DIM       lv_color_make(80,  80,  90)
#define COL_NODATA    lv_color_make(90,  90,  100)
#define COL_VOL_BG    lv_color_make(35,  37,  45)

/* ── Geometry ──────────────────────────────────────────────────── */
#define BTN_SIZE      56
#define BTN_BIG       64
#define BTN_GAP       16
#define VOL_BTN_W     48
#define VOL_BTN_H     36

/* ── State ─────────────────────────────────────────────────────── */
static bool is_playing = false;

/* ── UI objects ────────────────────────────────────────────────── */
static lv_obj_t *track_lbl   = NULL;
static lv_obj_t *artist_lbl  = NULL;
static lv_obj_t *album_lbl   = NULL;
static lv_obj_t *nodata_lbl  = NULL;

static lv_obj_t *btn_prev    = NULL;
static lv_obj_t *btn_play    = NULL;
static lv_obj_t *btn_next    = NULL;
static lv_obj_t *btn_vol_up  = NULL;
static lv_obj_t *btn_vol_dn  = NULL;

static lv_obj_t *play_icon   = NULL;

/* ── Progress bar (decorative, pulses when playing) ────────────── */
static lv_obj_t *prog_bar    = NULL;

/* ── Helpers ───────────────────────────────────────────────────── */
static void _set_play_icon(void) {
    if (!play_icon) return;
    if (is_playing) {
        lv_label_set_text(play_icon, LV_SYMBOL_PAUSE);
    } else {
        lv_label_set_text(play_icon, LV_SYMBOL_PLAY);
    }
}

/* ── Button callbacks ──────────────────────────────────────────── */
static void _btn_prev_cb(lv_event_t *e) {
    (void)e;
    gb_send_music_cmd("previous");
}

static void _btn_play_cb(lv_event_t *e) {
    (void)e;
    if (is_playing) {
        gb_send_music_cmd("pause");
    } else {
        gb_send_music_cmd("play");
    }
    /* Optimistic toggle – will be corrected by next musicstate msg */
    is_playing = !is_playing;
    _set_play_icon();
}

static void _btn_next_cb(lv_event_t *e) {
    (void)e;
    gb_send_music_cmd("next");
}

static void _btn_vol_up_cb(lv_event_t *e) {
    (void)e;
    gb_send_music_cmd("volumeup");
}

static void _btn_vol_dn_cb(lv_event_t *e) {
    (void)e;
    gb_send_music_cmd("volumedown");
}

/* ── Helper: create a round control button ─────────────────────── */
static lv_obj_t *_make_btn(lv_obj_t *parent, int w, int h,
                           const char *symbol, lv_color_t icon_col,
                           lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COL_BTN_BG, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, COL_BTN_PR, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, icon_col, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);

    if (lbl_out) *lbl_out = lbl;
    return btn;
}

/* ── Create ────────────────────────────────────────────────────── */
void media_screen_create(lv_obj_t *parent, int diameter)
{
    (void)diameter;
    int cx = 233;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "MEDIA");
    lv_obj_set_style_text_color(title, COL_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* ── Track info area ───────────────────────────────────── */
    /* Track name */
    track_lbl = lv_label_create(parent);
    lv_label_set_text(track_lbl, "");
    lv_obj_set_style_text_color(track_lbl, COL_TRACK, 0);
    lv_obj_set_style_text_font(track_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(track_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(track_lbl, 320);
    lv_label_set_long_mode(track_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(track_lbl, LV_ALIGN_CENTER, 0, -60);

    /* Artist */
    artist_lbl = lv_label_create(parent);
    lv_label_set_text(artist_lbl, "");
    lv_obj_set_style_text_color(artist_lbl, COL_ARTIST, 0);
    lv_obj_set_style_text_font(artist_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(artist_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(artist_lbl, 300);
    lv_label_set_long_mode(artist_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(artist_lbl, LV_ALIGN_CENTER, 0, -30);

    /* Album */
    album_lbl = lv_label_create(parent);
    lv_label_set_text(album_lbl, "");
    lv_obj_set_style_text_color(album_lbl, COL_ALBUM, 0);
    lv_obj_set_style_text_font(album_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(album_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(album_lbl, 280);
    lv_label_set_long_mode(album_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(album_lbl, LV_ALIGN_CENTER, 0, -4);

    /* No-data placeholder */
    nodata_lbl = lv_label_create(parent);
    lv_label_set_text(nodata_lbl, "No media playing");
    lv_obj_set_style_text_color(nodata_lbl, COL_NODATA, 0);
    lv_obj_set_style_text_font(nodata_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(nodata_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nodata_lbl, LV_ALIGN_CENTER, 0, -30);

    /* ── Decorative progress line ──────────────────────────── */
    prog_bar = lv_bar_create(parent);
    lv_obj_set_size(prog_bar, 260, 3);
    lv_bar_set_range(prog_bar, 0, 100);
    lv_bar_set_value(prog_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(prog_bar, COL_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(prog_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(prog_bar, COL_PLAY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(prog_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(prog_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(prog_bar, 2, LV_PART_INDICATOR);
    lv_obj_align(prog_bar, LV_ALIGN_CENTER, 0, 18);

    /* ── Transport controls (prev | play/pause | next) ──── */
    int y_ctrl = 65;

    btn_prev = _make_btn(parent, BTN_SIZE, BTN_SIZE,
                         LV_SYMBOL_PREV, COL_ICON,
                         _btn_prev_cb, NULL);
    lv_obj_align(btn_prev, LV_ALIGN_CENTER,
                 -(BTN_BIG/2 + BTN_GAP + BTN_SIZE/2), y_ctrl);

    btn_play = _make_btn(parent, BTN_BIG, BTN_BIG,
                         LV_SYMBOL_PLAY, COL_PLAY,
                         _btn_play_cb, &play_icon);
    lv_obj_set_style_text_font(play_icon, &lv_font_montserrat_28, 0);
    lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, y_ctrl);

    btn_next = _make_btn(parent, BTN_SIZE, BTN_SIZE,
                         LV_SYMBOL_NEXT, COL_ICON,
                         _btn_next_cb, NULL);
    lv_obj_align(btn_next, LV_ALIGN_CENTER,
                 (BTN_BIG/2 + BTN_GAP + BTN_SIZE/2), y_ctrl);

    /* ── Volume controls ───────────────────────────────────── */
    int y_vol = y_ctrl + BTN_BIG/2 + 24;

    btn_vol_dn = _make_btn(parent, VOL_BTN_W, VOL_BTN_H,
                           LV_SYMBOL_VOLUME_MID, COL_DIM,
                           _btn_vol_dn_cb, NULL);
    lv_obj_align(btn_vol_dn, LV_ALIGN_CENTER, -36, y_vol);

    btn_vol_up = _make_btn(parent, VOL_BTN_W, VOL_BTN_H,
                           LV_SYMBOL_VOLUME_MAX, COL_DIM,
                           _btn_vol_up_cb, NULL);
    lv_obj_align(btn_vol_up, LV_ALIGN_CENTER, 36, y_vol);

    /* initial state */
    _set_play_icon();
}

/* ── Update ────────────────────────────────────────────────────── */
void media_screen_update(const gb_music_info_t *info) {
    if (!info) return;

    bool has_track  = (info->track[0]  != '\0');
    bool has_artist = (info->artist[0] != '\0');

    /* Update play state */
    is_playing = info->playing;
    _set_play_icon();

    if (has_track || has_artist) {
        lv_obj_add_flag(nodata_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(track_lbl,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(artist_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(album_lbl,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(prog_bar,   LV_OBJ_FLAG_HIDDEN);

        if (has_track)
            lv_label_set_text(track_lbl, info->track);
        if (has_artist)
            lv_label_set_text(artist_lbl, info->artist);
        if (info->album[0] != '\0')
            lv_label_set_text(album_lbl, info->album);
        else
            lv_label_set_text(album_lbl, "");

        /* Animate progress bar: full when playing, empty when paused */
        lv_bar_set_value(prog_bar, is_playing ? 50 : 0, LV_ANIM_ON);
    } else {
        /* No track info at all – just update play state icon but
           still show placeholder if we have no text */
        if (lv_obj_has_flag(track_lbl, LV_OBJ_FLAG_HIDDEN) ||
            strlen(lv_label_get_text(track_lbl)) == 0) {
            lv_obj_clear_flag(nodata_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
