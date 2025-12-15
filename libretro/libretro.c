#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "libretro.h"

#include "emulator.h"
#include "gba.h"
#include "arm_isa.h"
#include "thumb_isa.h"

#ifndef VERSION
#define VERSION "0.1.0"
#endif

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

EmulatorState agbemu;

byte color_lookup[32];
byte color_lookup_filter[32];

void init_color_lookups() {
    for (int i = 0; i < 32; i++) {
        float c = (float) i / 31;
        color_lookup[i] = c * 255;
        color_lookup_filter[i] = pow(c, 1.7) * 255;
    }
}

void gba_convert_screen(hword* gba_screen, Uint32* screen) {
    for (int i = 0; i < GBA_SCREEN_W * GBA_SCREEN_H; i++) {
        int r = gba_screen[i] & 0x1f;
        int g = (gba_screen[i] >> 5) & 0x1f;
        int b = (gba_screen[i] >> 10) & 0x1f;
        if (agbemu.filter) {
            screen[i] = color_lookup_filter[r] << 16 |
                        color_lookup_filter[g] << 8 |
                        color_lookup_filter[b] << 0;
        } else {
            screen[i] = color_lookup[r] << 16 | color_lookup[g] << 8 |
                        color_lookup[b] << 0;
        }
    }
}

static char system_path[2048];
static char saves_path[2048];

static char game_path[2048];
static char save_path[2048];

static void concat(char* dest, int dsize, const char *s1, const char *s2)
{
  snprintf(dest, dsize - 1, "%s%s", s1, s2);
}

static void normalize_path(char* dest, int dsize, const char* path, bool add_slash)
{
  strcpy(dest, path);

  if (add_slash && dest[strlen(dest) - 1] != '/')
    strcat(dest, "/");

#ifdef WINDOWS
  for (char* p = dest; *p; p++)
    if (*p == '\\') *p = '/';
#endif
}

static void get_name_from_path(char* dest, int dsize, const char* path)
{
  strcpy(dest, strrchr(path, '/') + 1);

  char* delims[] = { ".zip#", ".7z#", ".apk#" };
  for (int i = 0; i < 3; i++)
  {
    char* delim_pos = strstr(dest, delims[i]);
    if (delim_pos) *delim_pos = '\0';
  }

  char* ext = strrchr(dest, '.');
  if (ext) *ext = '\0';
}

static void log_fallback(enum retro_log_level level, const char *fmt, ...)
{
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

static void reverse_eeprom_bytes(dword* eeprom, int size)
{
  for (int i = 0; i < size; i++)
  {
    dword x = eeprom[i];
    x = (x & 0xffffffff00000000) >> 32 | (x & 0x00000000ffffffff) << 32;
    x = (x & 0xffff0000ffff0000) >> 16 | (x & 0x0000ffff0000ffff) << 16;
    x = (x & 0xff00ff00ff00ff00) >> 8 | (x & 0x00ff00ff00ff00ff) << 8;
    eeprom[i] = x;
  }
}

static void load_save_file(Cartridge* cart, char* sav_filename)
{
  strcpy(cart->sav_filename, sav_filename);
  if (!cart->sav_size) return;

  cart->sram = malloc(cart->sav_size);
  FILE* fp = fopen(cart->sav_filename, "rb");
  if (fp)
  {
    (void) !fread(cart->sram, 1, cart->sav_size, fp);
    fclose(fp);
    if (cart->sav_type == SAV_EEPROM)
      reverse_eeprom_bytes(cart->eeprom, cart->sav_size / 8);
  }
  else memset(cart->sram, 0xff, cart->sav_size);
}

static char* fetch_variable(const char* key, const char* def)
{
  struct retro_variable var = {0};
  var.key = key;

  if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value == NULL)
  {
    log_cb(RETRO_LOG_WARN, "Fetching variable %s failed.", var.key);

    char* default_value = (char*)malloc(strlen(def) + 1);
    strcpy(default_value, def);

    return default_value;
  }

  char* value = (char*)malloc(strlen(var.value) + 1);
  strcpy(value, var.value);

  return value;
}

static bool fetch_variable_bool(const char* key, bool def)
{
  char* result = fetch_variable(key, def ? "enabled" : "disabled");
  bool is_enabled = strcmp(result, "enabled") == 0;

  if (result != NULL) {
    free(result);
    result = NULL;
  }
  return is_enabled;
}

static char* get_save_dir()
{
  char* dir = NULL;
  if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) || dir == NULL)
  {
    log_cb(RETRO_LOG_INFO, "No save directory provided by LibRetro.\n");
    return "agbemu";
  }
  return dir;
}

static char* get_system_dir()
{
  char* dir = NULL;
  if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || dir == NULL)
  {
    log_cb(RETRO_LOG_INFO, "No system directory provided by LibRetro.\n");
    return "agbemu";
  }
  return dir;
}

static bool get_button_state(unsigned id)
{
  return input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id);
}

static void init_input(void)
{
  static const struct retro_controller_description controllers[] = {
    { "Controller", RETRO_DEVICE_JOYPAD },
    { NULL, 0 },
  };

  static const struct retro_controller_info ports[] = {
    { controllers, 1 },
    { NULL, 0 },
  };

  environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

  struct retro_input_descriptor desc[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
    { 0 },
  };

  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

static void init_config()
{
  static const struct retro_variable values[] = {
    { "agbemu_boot_bios", "Boot bios on startup; enabled|disabled" },
    { "agbemu_uncaped_speed", "Run at uncapped speed; enabled|disabled" },
    { "agbemu_color_filter", "Apply color filter; disabled|enabled" },
    { NULL, NULL }
  };

  environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)values);
}

static void update_config()
{
  agbemu.bootbios = fetch_variable_bool("agbemu_boot_bios", true);
  agbemu.uncap = fetch_variable_bool("agbemu_uncaped_speed", true);
  agbemu.filter = fetch_variable_bool("agbemu_color_filter", false);
}

static void check_config_variables()
{
  bool updated = false;
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated);

  if (updated) update_config();
}

void retro_get_system_info(struct retro_system_info* info)
{
  info->need_fullpath = true;
  info->valid_extensions = "gba";
  info->library_version = VERSION;
  info->library_name = "agbemu";
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info)
{
  info->geometry.base_width = GBA_SCREEN_W;
  info->geometry.base_height = GBA_SCREEN_H;

  info->geometry.max_width = info->geometry.base_width;
  info->geometry.max_height = info->geometry.base_height;
  info->geometry.aspect_ratio = 3.0 / 2.0;

  info->timing.fps = 60.0;
  info->timing.sample_rate = SAMPLE_FREQ;
}

void retro_set_environment(retro_environment_t cb)
{
  environ_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
  video_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  audio_batch_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_input_poll(retro_input_poll_t cb)
{
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
  input_state_cb = cb;
}

void retro_init(void)
{
  enum retro_pixel_format xrgb888 = RETRO_PIXEL_FORMAT_XRGB8888;
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &xrgb888);

  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
    log_cb = logging.log;
  else
    log_cb = log_fallback;
  
  char tmp[2048];

  normalize_path(tmp, sizeof(tmp), get_system_dir(), true);
  snprintf(system_path, sizeof(system_path), "%s", tmp);

  normalize_path(tmp, sizeof(tmp), get_save_dir(), true);
  snprintf(saves_path, sizeof(system_path), "%s", tmp);
}

void retro_deinit(void)
{
  log_cb = NULL;
}

bool retro_load_game(const struct retro_game_info* info)
{
  char name[2048];
  char save[2048];

  get_name_from_path(name, sizeof(name), info->path);
  concat(save, sizeof(save), name, ".sav");

  char tmp[2048];

  normalize_path(tmp, sizeof(tmp), info->path, false);
  snprintf(game_path, sizeof(game_path), "%s", tmp);

  char p[2048];
  concat(p, sizeof(p), saves_path, save);
  normalize_path(tmp, sizeof(tmp), p, false);
  snprintf(save_path, sizeof(save_path), "%s", tmp);

  init_config();
  init_input();

  update_config();

  agbemu.romfile = game_path;

  char biosfile[2048];
  concat(biosfile, sizeof(biosfile), system_path, "gba_bios.bin");

  agbemu.gba = (GBA*)malloc(sizeof *agbemu.gba);
  agbemu.cart = create_cartridge(agbemu.romfile);

  if (!agbemu.cart)
  {
    if (agbemu.gba != NULL) {
      free(agbemu.gba);
      agbemu.gba = NULL;
    }
    log_cb(RETRO_LOG_ERROR, "Invalid rom file");

    return false;
  }

  agbemu.bios = load_bios(biosfile);

  if (!agbemu.bios)
  {
    if (agbemu.gba != NULL) {
      free(agbemu.gba);
      agbemu.gba = NULL;
    }
    destroy_cartridge(agbemu.cart);
    log_cb(RETRO_LOG_ERROR, "Invalid or missing bios file.");

    return false;
  }

  arm_generate_lookup();
  thumb_generate_lookup();

  init_color_lookups();

  load_save_file(agbemu.cart, save_path);
  init_gba(agbemu.gba, agbemu.cart, agbemu.bios, agbemu.bootbios);

  agbemu.running = true;
  agbemu.debugger = false;

  return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t info_size)
{
  return false;
}

void retro_unload_game(void)
{
  destroy_cartridge(agbemu.cart);
  if (agbemu.bios != NULL) {
    free(agbemu.bios);
    agbemu.bios = NULL;
  }
  if (agbemu.gba != NULL) {
    free(agbemu.gba);
    agbemu.gba = NULL;
  }
}

void retro_reset(void)
{
  gba_clear_ptrs(agbemu.gba);
  init_gba(agbemu.gba, agbemu.cart, agbemu.bios, agbemu.bootbios);
}

void retro_run(void)
{
  check_config_variables();
  input_poll_cb();

  agbemu.gba->io.keyinput.a = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_A);
  agbemu.gba->io.keyinput.b = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_B);
  agbemu.gba->io.keyinput.start = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_START);
  agbemu.gba->io.keyinput.select = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_SELECT);
  agbemu.gba->io.keyinput.left = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_LEFT);
  agbemu.gba->io.keyinput.right = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_RIGHT);
  agbemu.gba->io.keyinput.up = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_UP);
  agbemu.gba->io.keyinput.down = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_DOWN);
  agbemu.gba->io.keyinput.l = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_L);
  agbemu.gba->io.keyinput.r = ~(int)get_button_state(RETRO_DEVICE_ID_JOYPAD_R);

  static uint32_t pixels[GBA_SCREEN_W * GBA_SCREEN_H];
  static int16_t samples[SAMPLE_BUF_LEN];

  while (!agbemu.gba->stop && !agbemu.gba->ppu.frame_complete)
  {
    gba_step(agbemu.gba);

    if (agbemu.gba->apu.samples_full)
    {
      if (agbemu.gba->io.nr52 & (1 << 7))
        for (size_t i = 0; i < SAMPLE_BUF_LEN; i++)
          samples[i] = (int16_t)(agbemu.gba->apu.sample_buf[i] * 32767.0f);

      agbemu.gba->apu.samples_full = false;
    }
  }

  gba_convert_screen((hword*)agbemu.gba->ppu.screen, pixels);
  agbemu.gba->ppu.frame_complete = false;

  video_cb(pixels, GBA_SCREEN_W, GBA_SCREEN_H, GBA_SCREEN_W * 4);
  audio_batch_cb(samples, sizeof(samples) / (2 * sizeof(int16_t)));
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

size_t retro_serialize_size(void)
{
  return sizeof(*agbemu.gba) + sizeof(agbemu.cart->st);
}

bool retro_serialize(void* data, size_t size)
{
  gba_clear_ptrs(agbemu.gba);

  memcpy(data, agbemu.gba, sizeof(*agbemu.gba));
  memcpy((char*)data + sizeof(*agbemu.gba), &agbemu.cart->st, sizeof(agbemu.cart->st));

  gba_set_ptrs(agbemu.gba, agbemu.cart, agbemu.bios);
  return true;
}

bool retro_unserialize(const void* data, size_t size)
{
  gba_clear_ptrs(agbemu.gba);

  memcpy(agbemu.gba, data, sizeof(*agbemu.gba));
  memcpy(&agbemu.cart->st, (const char*)data + sizeof(*agbemu.gba), sizeof(agbemu.cart->st));

  gba_set_ptrs(agbemu.gba, agbemu.cart, agbemu.bios);
  return true;
}

unsigned retro_get_region(void)
{
  return RETRO_REGION_NTSC;
}

unsigned retro_api_version()
{
  return RETRO_API_VERSION;
}

size_t retro_get_memory_size(unsigned id)
{
  if (id == RETRO_MEMORY_SYSTEM_RAM)
  {
    return VRAM_SIZE;
  }
  return 0;
}

void* retro_get_memory_data(unsigned id)
{
  if (id == RETRO_MEMORY_SYSTEM_RAM)
  {
    return agbemu.gba->vram.b;
  }
  return NULL;
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
}

void retro_cheat_reset(void)
{
}
