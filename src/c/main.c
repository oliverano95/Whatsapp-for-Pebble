#include <pebble.h>

// ==============================================================================
// 1. DEFINES & CONSTANTS
// ==============================================================================
#define MAX_CHATS 10
#define MAX_MESSAGES 40
#define NUM_CANNED 6
#define NUM_REACTIONS 6

// ==============================================================================
// 2. GLOBAL VARIABLES
// ==============================================================================

// --- Theme State ---
static bool s_is_dark_mode = false;
#ifdef PBL_COLOR
static GColor s_color_me;
static GColor s_color_other;
#endif

// --- Data: Chats ---
static char *s_chat_names[MAX_CHATS];
static char *s_chat_previews[MAX_CHATS];
static int s_chat_unreads[MAX_CHATS];
static int s_num_chats = 0;
static int s_selected_chat_id = 0;

// --- Data: Messages ---
static char *s_mock_messages[MAX_MESSAGES];
static char *s_mock_senders[MAX_MESSAGES];
static char *s_mock_timestamps[MAX_MESSAGES];
static char *s_mock_reactions[MAX_MESSAGES];
static int s_mock_receipts[MAX_MESSAGES];
static int s_num_messages = 0;
static bool s_auto_scroll_to_bottom = true;

// --- Data: Actions & Replies ---
static int s_selected_message_idx = 0;
static int s_action_msg_idx = -1;
static bool s_action_msg_is_me = false;

static char reply_1_buffer[32] = "Yes";
static char reply_2_buffer[32] = "No";
static char reply_3_buffer[32] = "Call you later";
static char reply_4_buffer[32] = "OK";
static char reply_5_buffer[32] = "On my way!";
static char reply_6_buffer[32] = "Can't talk now.";
static char *s_canned_messages[] = {reply_1_buffer, reply_2_buffer, reply_3_buffer, reply_4_buffer, reply_5_buffer, reply_6_buffer};
static char s_reply_header_buffer[64];

static char *s_reaction_labels[] = {"👍 Like", "❤️ Love", "😂 Haha", "😮 Wow", "😢 Sad", "🙏 Pray"};
static char *s_reaction_values[] = {"0", "1", "2", "3", "4", "5"};

// --- UI Elements ---
static Window *s_main_window, *s_history_window, *s_canned_window, *s_reaction_window, *s_action_window;
static MenuLayer *s_menu_layer, *s_history_menu_layer, *s_canned_menu_layer, *s_reaction_menu_layer, *s_action_menu_layer;
static TextLayer *s_main_header_layer, *s_history_header_layer, *s_canned_header_layer, *s_reaction_header_layer;

#ifdef PBL_MICROPHONE
static DictationSession *s_dictation_session;
static char s_sent_message[512];
#endif


// ==============================================================================
// 3. FORWARD DECLARATIONS
// ==============================================================================
static void canned_window_push(void);
static void reaction_window_push(void);
static void action_window_push(void);
static void request_chat_history(int chat_id);
#ifdef PBL_MICROPHONE
static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context);
#endif


// ==============================================================================
// 4. MEMORY MANAGEMENT HELPERS
// ==============================================================================

// Safely free a string pointer and set it to NULL to prevent dangling pointers
static void safe_free(char **ptr) {
  if (*ptr) {
    free(*ptr);
    *ptr = NULL;
  }
}

static void free_chat_list() {
  for(int i = 0; i < s_num_chats; i++) {
    safe_free(&s_chat_names[i]);
    safe_free(&s_chat_previews[i]);
    s_chat_unreads[i] = 0;
  }
  s_num_chats = 0;
}

static void free_chat_history() {
  for(int i = 0; i < s_num_messages; i++) {
    safe_free(&s_mock_messages[i]);
    safe_free(&s_mock_senders[i]);
    safe_free(&s_mock_timestamps[i]);
    safe_free(&s_mock_reactions[i]);
    s_mock_receipts[i] = 0;
  }
  s_num_messages = 0;
}


// ==============================================================================
// 5. UI & THEME HELPERS
// ==============================================================================

// Consolidates the 20 lines of repetitive Dark Mode header logic
static void apply_theme_to_header(TextLayer *layer) {
  if (!layer) return;
  #ifdef PBL_COLOR
  text_layer_set_background_color(layer, s_is_dark_mode ? GColorDarkGray : (GColor){.argb = 0x40});
  text_layer_set_text_color(layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  text_layer_set_background_color(layer, s_is_dark_mode ? GColorBlack : GColorLightGray);
  text_layer_set_text_color(layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #endif
}

// Consolidates the repetitive Menu Layer color setup
static void apply_theme_to_menu(MenuLayer *layer, GColor light_highlight_color) {
  if (!layer) return;
  #ifdef PBL_COLOR
  menu_layer_set_normal_colors(layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(layer, s_is_dark_mode ? GColorDarkGreen : light_highlight_color, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  menu_layer_set_normal_colors(layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(layer, s_is_dark_mode ? GColorWhite : GColorBlack, s_is_dark_mode ? GColorBlack : GColorWhite);
  #endif
}

// Handles complex contrast flipping for standard menu rows
static void set_row_text_color(GContext* ctx, bool highlighted) {
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  if (s_is_dark_mode) {
    graphics_context_set_text_color(ctx, highlighted ? GColorBlack : GColorWhite);
  } else {
    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  }
  #endif
}

#ifdef PBL_COLOR
static GColor get_highlight_shade(GColor base) {
  if (base.r <= 1 && base.g <= 1 && base.b <= 1) {
    return (GColor){ .a = 3, .r = base.r+1, .g = base.g+1, .b = base.b+1 };
  } else {
    return (GColor){ .a = 3, .r = base.r > 0 ? base.r-1 : 0,
      .g = base.g > 0 ? base.g-1 : 0,
      .b = base.b > 0 ? base.b-1 : 0 };
  }
}
#endif


// ==============================================================================
// 6. DATA UTILITIES
// ==============================================================================
static uint32_t tuple_get_uint32(Tuple *t) {
  if(!t) return 0;
  if (t->type == TUPLE_INT) {
    if (t->length == 1) return (uint32_t)t->value->int8;
    if (t->length == 2) return (uint32_t)t->value->int16;
    if (t->length == 4) return (uint32_t)t->value->int32;
  } else if (t->type == TUPLE_UINT) {
    if (t->length == 1) return (uint32_t)t->value->uint8;
    if (t->length == 2) return (uint32_t)t->value->uint16;
    if (t->length == 4) return (uint32_t)t->value->uint32;
  }
  return 0;
}

static int get_message_index(uint16_t section_index, uint16_t row_index) {
  if (s_num_messages == 0) return 0;
  int current_section = 0, current_row = 0;
  for (int i = 0; i < s_num_messages; i++) {
    if (i > 0 && strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) {
      current_section++;
      current_row = 0;
    }
    if (current_section == section_index && current_row == row_index) return i;
    current_row++;
  }
  return 0;
}

static MenuIndex get_menu_index_from_msg_idx(int target_idx) {
  MenuIndex m_idx = {1, 0}; // Section 0 is the Load More button
  if (s_num_messages == 0 || target_idx < 0) return m_idx;
  for (int i = 1; i <= target_idx; i++) {
    if (strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) {
      m_idx.section++;
      m_idx.row = 0;
    } else {
      m_idx.row++;
    }
  }
  return m_idx;
}

static char* get_emoji_for_reaction(const char* safe_text) {
  if (!safe_text) return "";
  if (strstr(safe_text, "Like")) return "👍";
  if (strstr(safe_text, "Love")) return "❤️";
  if (strstr(safe_text, "Haha")) return "😂";
  if (strstr(safe_text, "Wow")) return "😮";
  if (strstr(safe_text, "Sad")) return "😢";
  if (strstr(safe_text, "Pray")) return "🙏";
  return "✨";
}


// ==============================================================================
// 7. BLUETOOTH TRANSMISSION APIS
// ==============================================================================
static void send_message_to_phone(int chat_id, const char *message) {
  DictionaryIterator *iter;
  if(app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 2; // Type 2 = Text Reply
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    dict_write_cstring(iter, MESSAGE_KEY_MESSAGE_TEXT, message);

    // Include the specific message index if we are quoting
    if (s_action_msg_idx != -1) {
      dict_write_int(iter, MESSAGE_KEY_INDEX, &s_action_msg_idx, sizeof(int), true);
    }
    app_message_outbox_send();

    // Optimistic UI Update!
    if (s_num_messages < MAX_MESSAGES) {
      int idx = s_num_messages;
      safe_free(&s_mock_senders[idx]);
      s_mock_senders[idx] = malloc(3); strcpy(s_mock_senders[idx], "Me");

      safe_free(&s_mock_messages[idx]);
      s_mock_messages[idx] = malloc(strlen(message) + 1); strcpy(s_mock_messages[idx], message);

      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char time_buf[10];
      strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);

      safe_free(&s_mock_timestamps[idx]);
      s_mock_timestamps[idx] = malloc(strlen(time_buf) + 1); strcpy(s_mock_timestamps[idx], time_buf);

      s_mock_receipts[idx] = 1; // Artificially inject "Sent" status
      s_num_messages++;

      if(s_history_menu_layer) {
        menu_layer_reload_data(s_history_menu_layer);
        MenuIndex last_idx = get_menu_index_from_msg_idx(s_num_messages - 1);
        menu_layer_set_selected_index(s_history_menu_layer, last_idx, MenuRowAlignBottom, false);
      }
    }
  }
  s_action_msg_idx = -1; // Reset action state
}

static void send_reaction_to_phone(int chat_id, int msg_idx, const char *reaction) {
  DictionaryIterator *iter;
  if(app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 4; // Type 4 = Reaction
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_INDEX, &msg_idx, sizeof(int), true);
    dict_write_cstring(iter, MESSAGE_KEY_MESSAGE_TEXT, reaction);
    app_message_outbox_send();
    vibes_short_pulse();
  }
}

static void send_delete_to_phone(int chat_id, int msg_idx) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 6; // Type 6 = Delete Message
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_INDEX, &msg_idx, sizeof(int), true);
    app_message_outbox_send();
    vibes_short_pulse();
  }
}

static void request_chat_history(int chat_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 1;
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    app_message_outbox_send();
  }
}

static void request_pagination(int chat_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 3; // Type 3 = Trigger Pagination fetch
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    app_message_outbox_send();
  }
}


// ==============================================================================
// 8. WINDOW: ACTION MENU
// ==============================================================================
static uint16_t action_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_action_msg_is_me ? 3 : 2;
}

static void action_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  (void)highlighted;

  set_row_text_color(ctx, highlighted);

  char *text = "";
  if (cell_index->row == 0) text = "React";
  else if (cell_index->row == 1) text = "Reply";
  else if (cell_index->row == 2) text = "Delete";

  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(8, 4, bounds.size.w - 16, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void action_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row == 0) {
    reaction_window_push();
  } else if (cell_index->row == 1) {
    canned_window_push();
  } else if (cell_index->row == 2) {
    send_delete_to_phone(s_selected_chat_id, s_action_msg_idx);
    window_stack_pop(true);
  }
}

static void action_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_action_menu_layer = menu_layer_create(bounds);
  apply_theme_to_menu(s_action_menu_layer, GColorMintGreen);

  menu_layer_set_callbacks(s_action_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = action_get_num_rows_callback,
    .draw_row = action_draw_row_callback,
    .select_click = action_select_callback,
  });

  menu_layer_set_click_config_onto_window(s_action_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_action_menu_layer));
}

static void action_window_unload(Window *window) {
  menu_layer_destroy(s_action_menu_layer);
}

static void action_window_push(void) {
  if(!s_action_window) {
    s_action_window = window_create();
    window_set_window_handlers(s_action_window, (WindowHandlers) {
      .load = action_window_load,
      .unload = action_window_unload,
    });
  }
  window_stack_push(s_action_window, true);
}


// ==============================================================================
// 9. WINDOW: REACTION
// ==============================================================================
static uint16_t reaction_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return NUM_REACTIONS;
}

static int16_t reaction_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return 44;
}

static void reaction_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  (void)highlighted;

  set_row_text_color(ctx, highlighted);

  graphics_draw_text(ctx, s_reaction_labels[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(8, 4, bounds.size.w - 16, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void reaction_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  send_reaction_to_phone(s_selected_chat_id, s_selected_message_idx, s_reaction_values[cell_index->row]);
  window_stack_pop(true);
}

static void reaction_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_reaction_header_layer = text_layer_create(GRect(0, 0, bounds.size.w, 24));
  text_layer_set_text(s_reaction_header_layer, "React to Message");
  text_layer_set_font(s_reaction_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_reaction_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_reaction_header_layer, GTextOverflowModeTrailingEllipsis);

  apply_theme_to_header(s_reaction_header_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_reaction_header_layer));

  s_reaction_menu_layer = menu_layer_create(GRect(0, 24, bounds.size.w, bounds.size.h - 24));
  apply_theme_to_menu(s_reaction_menu_layer, GColorMintGreen);

  menu_layer_set_callbacks(s_reaction_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = reaction_get_num_rows_callback,
    .get_cell_height = reaction_get_cell_height_callback,
    .draw_row = reaction_draw_row_callback,
    .select_click = reaction_select_callback,
  });

  menu_layer_set_click_config_onto_window(s_reaction_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_reaction_menu_layer));
}

static void reaction_window_unload(Window *window) {
  menu_layer_destroy(s_reaction_menu_layer);
  text_layer_destroy(s_reaction_header_layer);
}

static void reaction_window_push(void) {
  if(!s_reaction_window) {
    s_reaction_window = window_create();
    window_set_window_handlers(s_reaction_window, (WindowHandlers) {
      .load = reaction_window_load,
      .unload = reaction_window_unload
    });
  }
  window_stack_push(s_reaction_window, true);
}


// ==============================================================================
// 10. WINDOW: CANNED REPLIES / DICTATION
// ==============================================================================
static uint16_t canned_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  #ifdef PBL_MICROPHONE
  return NUM_CANNED + 1;
  #else
  return NUM_CANNED;
  #endif
}

static int16_t canned_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return 44;
}

static void canned_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  (void)highlighted;

  set_row_text_color(ctx, highlighted);

  char *text = NULL;
  #ifdef PBL_MICROPHONE
  if (cell_index->row == 0) {
    text = "🎤 Dictate reply...";
  } else {
    text = s_canned_messages[cell_index->row - 1];
  }
  #else
  text = s_canned_messages[cell_index->row];
  #endif

  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(8, 8, bounds.size.w - 16, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void canned_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  #ifdef PBL_MICROPHONE
  if (cell_index->row == 0) {
    dictation_session_start(s_dictation_session);
    return;
  }
  char *selected_message = s_canned_messages[cell_index->row - 1];
  #else
  char *selected_message = s_canned_messages[cell_index->row];
  #endif

  send_message_to_phone(s_selected_chat_id, selected_message);
  window_stack_pop(true);
}

static void canned_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_canned_header_layer = text_layer_create(GRect(0, 0, bounds.size.w, 24));
  snprintf(s_reply_header_buffer, sizeof(s_reply_header_buffer), "Reply to %s", s_chat_names[s_selected_chat_id]);
  text_layer_set_text(s_canned_header_layer, s_reply_header_buffer);
  text_layer_set_font(s_canned_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_canned_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_canned_header_layer, GTextOverflowModeTrailingEllipsis);

  apply_theme_to_header(s_canned_header_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_canned_header_layer));

  s_canned_menu_layer = menu_layer_create(GRect(0, 24, bounds.size.w, bounds.size.h - 24));
  apply_theme_to_menu(s_canned_menu_layer, GColorMintGreen);

  menu_layer_set_callbacks(s_canned_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = canned_get_num_rows_callback,
    .get_cell_height = canned_get_cell_height_callback,
    .draw_row = canned_draw_row_callback,
    .select_click = canned_select_callback,
  });

  menu_layer_set_click_config_onto_window(s_canned_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_canned_menu_layer));
}

static void canned_window_unload(Window *window) {
  menu_layer_destroy(s_canned_menu_layer);
  text_layer_destroy(s_canned_header_layer);
}

static void canned_window_push(void) {
  if(!s_canned_window) {
    s_canned_window = window_create();
    window_set_window_handlers(s_canned_window, (WindowHandlers) {
      .load = canned_window_load,
      .unload = canned_window_unload
    });
  }
  window_stack_push(s_canned_window, true);
}


// ==============================================================================
// 11. WINDOW: CHAT HISTORY
// ==============================================================================
static uint16_t history_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  if (s_num_messages == 0) return 1;
  uint16_t sections = 1;
  for (int i = 1; i < s_num_messages; i++) {
    if (strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) sections++;
  }
  return sections + 1; // +1 for "Load More" section
}

static uint16_t history_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  if (s_num_messages == 0 || section_index == 0) return 1;

  int real_section = section_index - 1;
  int current_section = 0, count = 1;
  for (int i = 1; i < s_num_messages; i++) {
    if (strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) {
      if (current_section == real_section) return count;
      current_section++;
      count = 1;
    } else {
      count++;
    }
  }
  return count;
}

static int16_t history_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  if (s_num_messages == 0 || section_index == 0) return 0;

  int msg_idx = get_message_index(section_index - 1, 0);
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);
  bool is_1on1 = (strcmp(s_mock_senders[msg_idx], s_chat_names[s_selected_chat_id]) == 0);

  if (is_me || is_1on1) return 0;
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static int16_t history_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0) return 32;
  if (cell_index->section == 0) return 44;

  int msg_idx = get_message_index(cell_index->section - 1, cell_index->row);
  GRect bounds = layer_get_bounds(menu_layer_get_layer(menu_layer));

  GSize text_size = graphics_text_layout_get_content_size(
    s_mock_messages[msg_idx], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                                                          GRect(0, 0, bounds.size.w - 32, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft
  );

  int reaction_padding = (s_mock_reactions[msg_idx] != NULL) ? 10 : 0;
  int16_t calculated_height = text_size.h + 12 + 16 + 4 + reaction_padding;

  return (calculated_height > 44) ? calculated_height : 44;
}

static void history_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  if (s_num_messages == 0 || section_index == 0) return;

  int msg_idx = get_message_index(section_index - 1, 0);
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);
  bool is_1on1 = (strcmp(s_mock_senders[msg_idx], s_chat_names[s_selected_chat_id]) == 0);

  if (is_me || is_1on1) return;

  GRect bounds = layer_get_bounds(cell_layer);
  graphics_context_set_fill_color(ctx, s_is_dark_mode ? GColorBlack : GColorWhite);
  graphics_context_set_text_color(ctx, s_is_dark_mode ? GColorWhite : GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_draw_text(ctx, s_mock_senders[msg_idx], fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     grect_inset(bounds, GEdgeInsets(2, 4, 2, 4)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void history_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);

  // Paint background to perfectly match theme
  graphics_context_set_fill_color(ctx, s_is_dark_mode ? GColorBlack : GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_num_messages == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "Loading messages...", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // Draw "Load More" Button
  if (cell_index->section == 0) {
    bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, highlighted ? GColorMintGreen : GColorLightGray);
    graphics_context_set_text_color(ctx, GColorBlack);
    #else
    graphics_context_set_fill_color(ctx, highlighted ? GColorBlack : GColorLightGray);
    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    #endif

    graphics_fill_rect(ctx, grect_inset(bounds, GEdgeInsets(6, 16, 6, 16)), 16, GCornersAll);
    graphics_draw_text(ctx, "Load older messages...", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(0, 12, bounds.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // Message Bubble Logic
  int msg_idx = get_message_index(cell_index->section - 1, cell_index->row);
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);

  MenuIndex selected_index = menu_layer_get_selected_index(s_history_menu_layer);
  bool highlighted = (cell_index->section == selected_index.section && cell_index->row == selected_index.row);

  GSize text_size = graphics_text_layout_get_content_size(
    s_mock_messages[msg_idx], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                                                          GRect(0, 0, bounds.size.w - 32, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft
  );

  bool has_reaction = (s_mock_reactions[msg_idx] != NULL);

  int padding_x = 8, padding_y = 6, timestamp_h = 16, margin_x = 4;
  int bubble_w = text_size.w + (padding_x * 2);
  if (bubble_w < 60) bubble_w = 60;
  int bubble_h = text_size.h + (padding_y * 2) + timestamp_h;
  int bubble_x = is_me ? (bounds.size.w - bubble_w - margin_x) : margin_x;
  GRect bubble_rect = GRect(bubble_x, 2, bubble_w, bubble_h);

  GColor bubble_color, text_color, time_color;

  #ifdef PBL_COLOR
  GColor base_color = is_me ? s_color_me : s_color_other;
  bubble_color = highlighted ? get_highlight_shade(base_color) : base_color;
  text_color = gcolor_legible_over(bubble_color);
  time_color = gcolor_equal(text_color, GColorWhite) ? GColorLightGray : GColorDarkGray;
  #else
  if (highlighted) {
    bubble_color = GColorBlack; text_color = GColorWhite; time_color = GColorWhite;
  } else {
    bubble_color = is_me ? GColorWhite : GColorLightGray; text_color = GColorBlack; time_color = GColorBlack;
  }
  #endif

  // Draw Bubble & Tail
  graphics_context_set_fill_color(ctx, bubble_color);
  graphics_fill_rect(ctx, bubble_rect, 8, GCornersAll);

  if (cell_index->row == 0) {
    graphics_context_set_stroke_color(ctx, bubble_color);
    int tail_h = 10, tail_w = 4;
    if (is_me) {
      for (int i = 0; i < tail_h; i++)
        graphics_draw_line(ctx, GPoint(bubble_x + bubble_w - 6, 2 + i), GPoint(bubble_x + bubble_w + tail_w - i, 2 + i));
    } else {
      for (int i = 0; i < tail_h; i++)
        graphics_draw_line(ctx, GPoint(bubble_x - tail_w + i, 2 + i), GPoint(bubble_x + 6, 2 + i));
    }
  }

  #ifndef PBL_COLOR
  if (!highlighted && is_me) {
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_round_rect(ctx, bubble_rect, 8);
  }
  #endif

  // Draw Message Text
  GRect text_rect = GRect(bubble_x + padding_x, padding_y, bubble_w - (padding_x * 2), text_size.h);
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx, s_mock_messages[msg_idx], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     text_rect, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Draw Timestamp
  int check_space = (is_me && s_mock_receipts[msg_idx] > 0) ? 14 : 0;
  GRect time_rect = GRect(bubble_x + padding_x, text_rect.origin.y + text_size.h, bubble_w - (padding_x * 2) - check_space, timestamp_h);
  graphics_context_set_text_color(ctx, time_color);
  graphics_draw_text(ctx, s_mock_timestamps[msg_idx], fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     time_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // Draw Read Receipts
  if (is_me && s_mock_receipts[msg_idx] > 0) {
    int receipt = s_mock_receipts[msg_idx];
    GColor check_color = time_color;

    #ifdef PBL_COLOR
    if (receipt >= 3) check_color = gcolor_equal(text_color, GColorBlack) ? GColorBlue : GColorCyan;
    #endif

    graphics_context_set_stroke_color(ctx, check_color);
    int cx = bubble_x + bubble_w - padding_x - 12, cy = time_rect.origin.y + 7;
    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(cx + 2, cy + 3));
    graphics_draw_line(ctx, GPoint(cx + 2, cy + 3), GPoint(cx + 6, cy - 3));

    if (receipt >= 2) {
      int cx2 = cx + 5;
      graphics_draw_line(ctx, GPoint(cx2, cy), GPoint(cx2 + 2, cy + 3));
      graphics_draw_line(ctx, GPoint(cx2 + 2, cy + 3), GPoint(cx2 + 6, cy - 3));
    }
  }

  // Draw Reaction Badge
  if (has_reaction) {
    char* display_emoji = get_emoji_for_reaction(s_mock_reactions[msg_idx]);
    int rw = 28, rh = 18;
    GRect reaction_rect = GRect(bubble_x + bubble_w - rw + 4, bubble_rect.origin.y + bubble_rect.size.h - 8, rw, rh);

    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, (strcmp(display_emoji, "❤️") == 0) ? GColorMelon : GColorPastelYellow);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    #endif

    graphics_fill_rect(ctx, reaction_rect, 9, GCornersAll);
    graphics_draw_round_rect(ctx, reaction_rect, 9);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, display_emoji, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(reaction_rect.origin.x, reaction_rect.origin.y - 7, rw, rh + 7),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void history_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0) return;

  if (cell_index->section == 0) {
    s_auto_scroll_to_bottom = false;
    request_pagination(s_selected_chat_id);
    return;
  }

  s_action_msg_idx = -1;
  canned_window_push();
}

static void history_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0 || cell_index->section == 0) return;

  s_selected_message_idx = get_message_index(cell_index->section - 1, cell_index->row);
  s_action_msg_idx = s_selected_message_idx;
  s_action_msg_is_me = (strcmp(s_mock_senders[s_selected_message_idx], "Me") == 0);

  action_window_push();
}

static void history_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_history_header_layer = text_layer_create(GRect(0, 0, bounds.size.w, 24));
  text_layer_set_text(s_history_header_layer, s_chat_names[s_selected_chat_id]);
  text_layer_set_font(s_history_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_history_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_history_header_layer, GTextOverflowModeTrailingEllipsis);

  apply_theme_to_header(s_history_header_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_history_header_layer));

  s_history_menu_layer = menu_layer_create(GRect(0, 24, bounds.size.w, bounds.size.h - 24));
  apply_theme_to_menu(s_history_menu_layer, GColorWhite); // History highlight matches background

  menu_layer_set_callbacks(s_history_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = history_get_num_sections_callback,
    .get_header_height = history_get_header_height_callback,
    .draw_header = history_draw_header_callback,
    .get_num_rows = history_get_num_rows_callback,
    .get_cell_height = history_get_cell_height_callback,
    .draw_row = history_draw_row_callback,
    .select_click = history_select_callback,
    .select_long_click = history_select_long_callback,
  });

  menu_layer_set_click_config_onto_window(s_history_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_history_menu_layer));
}

static void history_window_unload(Window *window) {
  menu_layer_destroy(s_history_menu_layer);
  text_layer_destroy(s_history_header_layer);
}


// ==============================================================================
// 12. WINDOW: MAIN MENU (CHAT LIST)
// ==============================================================================
static int16_t menu_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return s_num_chats == 0 ? layer_get_bounds(menu_layer_get_layer(menu_layer)).size.h : 60;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_chats == 0 ? 1 : s_num_chats;
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  (void)highlighted;

  if (s_num_chats == 0) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorDarkGreen);
    #else
    graphics_context_set_text_color(ctx, GColorBlack);
    #endif

    int16_t mid_y = bounds.size.h / 2;
    graphics_draw_text(ctx, "Loading Chats...", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, mid_y - 28, bounds.size.w, 28), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "Please wait", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(0, mid_y, bounds.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  GColor title_color, preview_color;
  #ifdef PBL_COLOR
  title_color = s_is_dark_mode ? GColorWhite : GColorBlack;
  preview_color = s_is_dark_mode ? GColorLightGray : GColorDarkGray;
  #else
  title_color = highlighted ? GColorWhite : (s_is_dark_mode ? GColorWhite : GColorBlack);
  preview_color = highlighted ? GColorWhite : (s_is_dark_mode ? GColorLightGray : GColorBlack);
  #endif

  int text_width = bounds.size.w - 16;
  int unread_count = s_chat_unreads[cell_index->row];

  if (unread_count > 0) {
    int badge_radius = 11;
    int badge_x = bounds.size.w - badge_radius - 8, badge_y = bounds.size.h / 2;
    text_width -= (badge_radius * 2 + 8);

    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorKellyGreen);
    graphics_context_set_text_color(ctx, GColorWhite);
    #else
    graphics_context_set_fill_color(ctx, highlighted ? GColorWhite : GColorBlack);
    graphics_context_set_text_color(ctx, highlighted ? GColorBlack : GColorWhite);
    #endif

    graphics_fill_circle(ctx, GPoint(badge_x, badge_y), badge_radius);
    char unread_str[4];
    snprintf(unread_str, sizeof(unread_str), "%d", unread_count > 99 ? 99 : unread_count);
    graphics_draw_text(ctx, unread_str, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(badge_x - 10, badge_y - 10, 20, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  graphics_context_set_text_color(ctx, title_color);
  graphics_draw_text(ctx, s_chat_names[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(8, 4, text_width, 28), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, preview_color);
  graphics_draw_text(ctx, s_chat_previews[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(8, 32, text_width, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_chats == 0) return;

  s_selected_chat_id = cell_index->row;
  s_auto_scroll_to_bottom = true;
  s_chat_unreads[s_selected_chat_id] = 0;
  menu_layer_reload_data(s_menu_layer);

  free_chat_history();
  if(s_history_menu_layer) menu_layer_reload_data(s_history_menu_layer);

  request_chat_history(s_selected_chat_id);

  if(!s_history_window) {
    s_history_window = window_create();
    window_set_window_handlers(s_history_window, (WindowHandlers) {
      .load = history_window_load,
      .unload = history_window_unload
    });
  }
  window_stack_push(s_history_window, true);
}

static void main_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_main_header_layer = text_layer_create(GRect(0, 0, bounds.size.w, 24));
  text_layer_set_text(s_main_header_layer, "WhatsApp");
  text_layer_set_font(s_main_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_main_header_layer, GTextAlignmentCenter);

  apply_theme_to_header(s_main_header_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_main_header_layer));

  s_menu_layer = menu_layer_create(GRect(0, 24, bounds.size.w, bounds.size.h - 24));
  apply_theme_to_menu(s_menu_layer, GColorMintGreen);

  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_get_num_rows_callback,
    .get_cell_height = menu_get_cell_height_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
  });

  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  #ifdef PBL_MICROPHONE
  s_dictation_session = dictation_session_create(sizeof(s_sent_message), dictation_session_callback, NULL);
  dictation_session_enable_confirmation(s_dictation_session, true);
  #endif
}

static void main_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_main_header_layer);
  #ifdef PBL_MICROPHONE
  dictation_session_destroy(s_dictation_session);
  #endif
}


// ==============================================================================
// 13. APP MESSAGE EVENT LISTENERS
// ==============================================================================
#ifdef PBL_MICROPHONE
static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if(status == DictationSessionStatusSuccess) {
    snprintf(s_sent_message, sizeof(s_sent_message), "%s", transcription);
    send_message_to_phone(s_selected_chat_id, s_sent_message);
    window_stack_pop(true);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Dictation failed with status: %d", (int)status);
  }
}
#endif

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {

  // Load Custom Replies
  Tuple *r_tuples[] = {
    dict_find(iterator, MESSAGE_KEY_REPLY_1), dict_find(iterator, MESSAGE_KEY_REPLY_2),
    dict_find(iterator, MESSAGE_KEY_REPLY_3), dict_find(iterator, MESSAGE_KEY_REPLY_4),
    dict_find(iterator, MESSAGE_KEY_REPLY_5), dict_find(iterator, MESSAGE_KEY_REPLY_6)
  };
  char* r_buffers[] = {reply_1_buffer, reply_2_buffer, reply_3_buffer, reply_4_buffer, reply_5_buffer, reply_6_buffer};
  uint32_t r_keys[] = {MESSAGE_KEY_REPLY_1, MESSAGE_KEY_REPLY_2, MESSAGE_KEY_REPLY_3, MESSAGE_KEY_REPLY_4, MESSAGE_KEY_REPLY_5, MESSAGE_KEY_REPLY_6};

  for(int i=0; i<6; i++) {
    if(r_tuples[i]) {
      snprintf(r_buffers[i], 32, "%s", r_tuples[i]->value->cstring);
      persist_write_string(r_keys[i], r_buffers[i]);
    }
  }

  // Load Theme
  Tuple *dark_tuple = dict_find(iterator, MESSAGE_KEY_DARK_MODE);
  if (dark_tuple) {
    s_is_dark_mode = (dark_tuple->value->int32 == 1);
    persist_write_bool(MESSAGE_KEY_DARK_MODE, s_is_dark_mode);

    // Force repaint
    if (s_main_window) window_set_background_color(s_main_window, s_is_dark_mode ? GColorBlack : GColorWhite);
    apply_theme_to_header(s_main_header_layer);
    if (s_menu_layer) {
      apply_theme_to_menu(s_menu_layer, GColorMintGreen);
      menu_layer_reload_data(s_menu_layer);
    }
  }

  #ifdef PBL_COLOR
  Tuple *theme_me_tuple = dict_find(iterator, MESSAGE_KEY_THEME_ME);
  if (theme_me_tuple) {
    uint32_t raw_val = tuple_get_uint32(theme_me_tuple);
    s_color_me = GColorFromHEX(raw_val);
    persist_write_int(MESSAGE_KEY_THEME_ME, raw_val);
  }
  Tuple *theme_other_tuple = dict_find(iterator, MESSAGE_KEY_THEME_OTHER);
  if (theme_other_tuple) {
    uint32_t raw_val = tuple_get_uint32(theme_other_tuple);
    s_color_other = GColorFromHEX(raw_val);
    persist_write_int(MESSAGE_KEY_THEME_OTHER, raw_val);
  }
  #endif

  // Process Server Chat Data
  Tuple *type_tuple = dict_find(iterator, MESSAGE_KEY_DATA_TYPE);
  if(!type_tuple) {
    if(s_history_menu_layer) menu_layer_reload_data(s_history_menu_layer);
    return;
  }

  int type = type_tuple->value->int32;
  Tuple *index_tuple = dict_find(iterator, MESSAGE_KEY_INDEX);
  int index = index_tuple ? index_tuple->value->int32 : 0;

  if(type == 0) { // Chat List Data
    if(index == 0) free_chat_list();

    Tuple *name_t = dict_find(iterator, MESSAGE_KEY_CHAT_NAME);
    Tuple *preview_t = dict_find(iterator, MESSAGE_KEY_CHAT_PREVIEW);
    Tuple *unread_t = dict_find(iterator, MESSAGE_KEY_UNREAD_COUNT);

    if(name_t && preview_t && index < MAX_CHATS) {
      safe_free(&s_chat_names[index]);
      s_chat_names[index] = malloc(strlen(name_t->value->cstring) + 1);
      strcpy(s_chat_names[index], name_t->value->cstring);

      safe_free(&s_chat_previews[index]);
      s_chat_previews[index] = malloc(strlen(preview_t->value->cstring) + 1);
      strcpy(s_chat_previews[index], preview_t->value->cstring);

      s_chat_unreads[index] = unread_t ? unread_t->value->int32 : 0;

      s_num_chats = index + 1;
      if(s_menu_layer) menu_layer_reload_data(s_menu_layer);
    }
  } else if(type == 1) { // Chat History Data
    if(index == 0) free_chat_history();

    Tuple *sender_t = dict_find(iterator, MESSAGE_KEY_SENDER_NAME);
    Tuple *msg_t = dict_find(iterator, MESSAGE_KEY_MESSAGE_TEXT);
    Tuple *ts_t = dict_find(iterator, MESSAGE_KEY_TIMESTAMP);
    Tuple *receipt_t = dict_find(iterator, MESSAGE_KEY_RECEIPT_STATUS);

    if(sender_t && msg_t && index < MAX_MESSAGES) {
      safe_free(&s_mock_senders[index]);
      s_mock_senders[index] = malloc(strlen(sender_t->value->cstring) + 1);
      strcpy(s_mock_senders[index], sender_t->value->cstring);

      safe_free(&s_mock_messages[index]);
      s_mock_messages[index] = malloc(strlen(msg_t->value->cstring) + 1);
      strcpy(s_mock_messages[index], msg_t->value->cstring);

      char *time_str = ts_t ? ts_t->value->cstring : "12:00";
      safe_free(&s_mock_timestamps[index]);
      s_mock_timestamps[index] = malloc(strlen(time_str) + 1);
      strcpy(s_mock_timestamps[index], time_str);

      s_mock_receipts[index] = receipt_t ? receipt_t->value->int32 : 0;

      safe_free(&s_mock_reactions[index]);
      Tuple *reaction_t = dict_find(iterator, MESSAGE_KEY_REACTION);
      if (reaction_t && strlen(reaction_t->value->cstring) > 0) {
        s_mock_reactions[index] = malloc(strlen(reaction_t->value->cstring) + 1);
        strcpy(s_mock_reactions[index], reaction_t->value->cstring);
      }

      s_num_messages = index + 1;
      if(s_history_menu_layer) menu_layer_reload_data(s_history_menu_layer);
    }
  } else if (type == 5) { // End of History Marker
    if(s_history_menu_layer && s_auto_scroll_to_bottom && s_num_messages > 0) {
      MenuIndex last_idx = get_menu_index_from_msg_idx(s_num_messages - 1);
      menu_layer_set_selected_index(s_history_menu_layer, last_idx, MenuRowAlignBottom, false);
    }
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped! Reason: %d", (int)reason);
}
static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Message successfully sent to phone!");
}
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! Reason: %d", (int)reason);
}


// ==============================================================================
// 14. APP LIFECYCLE
// ==============================================================================
static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Load Persisted Storage
  if (persist_exists(MESSAGE_KEY_REPLY_1)) persist_read_string(MESSAGE_KEY_REPLY_1, reply_1_buffer, sizeof(reply_1_buffer));
  if (persist_exists(MESSAGE_KEY_REPLY_2)) persist_read_string(MESSAGE_KEY_REPLY_2, reply_2_buffer, sizeof(reply_2_buffer));
  if (persist_exists(MESSAGE_KEY_REPLY_3)) persist_read_string(MESSAGE_KEY_REPLY_3, reply_3_buffer, sizeof(reply_3_buffer));
  if (persist_exists(MESSAGE_KEY_REPLY_4)) persist_read_string(MESSAGE_KEY_REPLY_4, reply_4_buffer, sizeof(reply_4_buffer));
  if (persist_exists(MESSAGE_KEY_REPLY_5)) persist_read_string(MESSAGE_KEY_REPLY_5, reply_5_buffer, sizeof(reply_5_buffer));
  if (persist_exists(MESSAGE_KEY_REPLY_6)) persist_read_string(MESSAGE_KEY_REPLY_6, reply_6_buffer, sizeof(reply_6_buffer));
  if (persist_exists(MESSAGE_KEY_DARK_MODE)) s_is_dark_mode = persist_read_bool(MESSAGE_KEY_DARK_MODE);

  #ifdef PBL_COLOR
  s_color_me = persist_exists(MESSAGE_KEY_THEME_ME) ? GColorFromHEX(persist_read_int(MESSAGE_KEY_THEME_ME)) : GColorKellyGreen;
  s_color_other = persist_exists(MESSAGE_KEY_THEME_OTHER) ? GColorFromHEX(persist_read_int(MESSAGE_KEY_THEME_OTHER)) : GColorLightGray;
  #endif

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(1024, 1024);

  window_stack_push(s_main_window, true);
}

static void deinit() {
  if(s_main_window) window_destroy(s_main_window);
  if(s_canned_window) window_destroy(s_canned_window);
  if(s_reaction_window) window_destroy(s_reaction_window);
  if(s_action_window) window_destroy(s_action_window); // FIXED: Memory Leak
  if(s_history_window) window_destroy(s_history_window);

  free_chat_list();
  free_chat_history();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
