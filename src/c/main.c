#include <pebble.h>

// UI Elements
static Window *s_main_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_main_header_layer; // NEW: Persistent top header for main menu!

// Dictation Elements
#ifdef PBL_MICROPHONE
static DictationSession *s_dictation_session;
static char s_sent_message[512];
#endif

// Dynamic Chat List Data
#define MAX_CHATS 10
static char *s_chat_names[MAX_CHATS];
static char *s_chat_previews[MAX_CHATS];
static int s_chat_unreads[MAX_CHATS]; // NEW: Store the unread count!
static int s_num_chats = 0;

// Dynamic Chat History Data
#define MAX_MESSAGES 40 // INCREASED: Allow up to 40 messages via pagination!
static char *s_mock_messages[MAX_MESSAGES];
static char *s_mock_senders[MAX_MESSAGES];
static char *s_mock_timestamps[MAX_MESSAGES]; 
#define MESSAGE_KEY_REACTION 10005
static char *s_mock_reactions[MAX_MESSAGES];
static int s_mock_receipts[MAX_MESSAGES]; 
static bool s_is_dark_mode = false;
static int s_num_messages = 0;
static bool s_auto_scroll_to_bottom = true; // NEW: Track if we should snap to bottom!

// --- 1. CUSTOM REPLY & THEME BUFFERS ---
static char reply_1_buffer[32] = "Yes";
static char reply_2_buffer[32] = "No";
static char reply_3_buffer[32] = "Call you later";
static char reply_4_buffer[32] = "OK";
static char reply_5_buffer[32] = "On my way!";
static char reply_6_buffer[32] = "Can't talk now.";

#ifdef PBL_COLOR
static GColor s_color_me;
static GColor s_color_other;
#endif

// Canned Responses Elements
static Window *s_canned_window;
static MenuLayer *s_canned_menu_layer;
static TextLayer *s_canned_header_layer; // NEW: Header for replies
static char s_reply_header_buffer[64];   // NEW: Buffer for "Reply to X" text
#define NUM_CANNED 6 
// --- 2. POINT THE MENU TO THE BUFFERS ---
static char *s_canned_messages[] = {reply_1_buffer, reply_2_buffer, reply_3_buffer, reply_4_buffer, reply_5_buffer, reply_6_buffer};
static int s_selected_chat_id = 0;

// --- 3. REACTION ELEMENTS ---
static Window *s_reaction_window;
static MenuLayer *s_reaction_menu_layer;
static TextLayer *s_reaction_header_layer;
static int s_selected_message_idx = 0; // Track which message we are reacting to

// Labels for the watch UI
static char *s_reaction_labels[] = {"👍 Like", "❤️ Love", "😂 Haha", "😮 Wow", "😢 Sad", "🙏 Pray"};

// THE FIX: Raw numeric strings sent to the Node.js server to avoid Bluetooth corruption!
static char *s_reaction_values[] = {"0", "1", "2", "3", "4", "5"};
#define NUM_REACTIONS 6

// Chat History Elements
static Window *s_history_window;
static MenuLayer *s_history_menu_layer;
static TextLayer *s_history_header_layer; 

// --- TUPLE EXTRACTION HELPER ---
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

// --- MENU INDEX HELPERS ---
static int get_message_index(uint16_t section_index, uint16_t row_index) {
  if (s_num_messages == 0) return 0;
  int current_section = 0;
  int current_row = 0;
  for (int i = 0; i < s_num_messages; i++) {
    if (i > 0 && strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) {
      current_section++;
      current_row = 0;
    }
    if (current_section == section_index && current_row == row_index) {
      return i;
    }
    current_row++;
  }
  return 0;
}

static MenuIndex get_menu_index_from_msg_idx(int target_idx) {
  MenuIndex m_idx = {1, 0}; // START AT 1: Section 0 is now the Load More button!
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

// --- MEMORY MANAGEMENT ---
static void free_chat_list() {
  for(int i = 0; i < s_num_chats; i++) {
    if(s_chat_names[i]) { free(s_chat_names[i]); s_chat_names[i] = NULL; }
    if(s_chat_previews[i]) { free(s_chat_previews[i]); s_chat_previews[i] = NULL; }
    s_chat_unreads[i] = 0;
  }
  s_num_chats = 0;
}

static void free_chat_history() {
  for(int i = 0; i < s_num_messages; i++) {
    if(s_mock_messages[i]) { free(s_mock_messages[i]); s_mock_messages[i] = NULL; }
    if(s_mock_senders[i]) { free(s_mock_senders[i]); s_mock_senders[i] = NULL; }
    if(s_mock_timestamps[i]) { free(s_mock_timestamps[i]); s_mock_timestamps[i] = NULL; }
    s_mock_receipts[i] = 0;
    if(s_mock_reactions[i]) { free(s_mock_reactions[i]); s_mock_reactions[i] = NULL; }
  }
  s_num_messages = 0;
}

// --- APPMESSAGE CALLBACKS ---
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  
  // --- 3. CATCH CUSTOM REPLIES & THEMES FROM CLAY ---
  Tuple *reply1_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_1);
  if (reply1_tuple) {
    snprintf(reply_1_buffer, sizeof(reply_1_buffer), "%s", reply1_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_1, reply_1_buffer);
  }

  Tuple *reply2_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_2);
  if (reply2_tuple) {
    snprintf(reply_2_buffer, sizeof(reply_2_buffer), "%s", reply2_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_2, reply_2_buffer);
  }

  Tuple *reply3_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_3);
  if (reply3_tuple) {
    snprintf(reply_3_buffer, sizeof(reply_3_buffer), "%s", reply3_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_3, reply_3_buffer);
  }

  Tuple *reply4_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_4);
  if (reply4_tuple) {
    snprintf(reply_4_buffer, sizeof(reply_4_buffer), "%s", reply4_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_4, reply_4_buffer);
  }

  Tuple *reply5_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_5);
  if (reply5_tuple) {
    snprintf(reply_5_buffer, sizeof(reply_5_buffer), "%s", reply5_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_5, reply_5_buffer);
  }

  Tuple *reply6_tuple = dict_find(iterator, MESSAGE_KEY_REPLY_6);
  if (reply6_tuple) {
    snprintf(reply_6_buffer, sizeof(reply_6_buffer), "%s", reply6_tuple->value->cstring);
    persist_write_string(MESSAGE_KEY_REPLY_6, reply_6_buffer);
  }

  Tuple *dark_tuple = dict_find(iterator, MESSAGE_KEY_DARK_MODE);
  if (dark_tuple) {
    s_is_dark_mode = (dark_tuple->value->int32 == 1);
    persist_write_bool(MESSAGE_KEY_DARK_MODE, s_is_dark_mode);

    // Force the UI to repaint immediately!
    if (s_main_window) {
      window_set_background_color(s_main_window, s_is_dark_mode ? GColorBlack : GColorWhite);
    }
    if (s_main_header_layer) {
      #ifdef PBL_COLOR
      text_layer_set_background_color(s_main_header_layer, s_is_dark_mode ? GColorDarkGray : (GColor){.argb = 0x40});
      text_layer_set_text_color(s_main_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
      #else
      text_layer_set_background_color(s_main_header_layer, s_is_dark_mode ? GColorBlack : GColorLightGray);
      text_layer_set_text_color(s_main_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
      #endif
    }
    if (s_menu_layer) {
      #ifdef PBL_COLOR
      menu_layer_set_normal_colors(s_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
      menu_layer_set_highlight_colors(s_menu_layer, s_is_dark_mode ? GColorDarkGreen : GColorMintGreen, s_is_dark_mode ? GColorWhite : GColorBlack);
      #endif
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

  // Now, check if this is normal chat data from the server
  Tuple *type_tuple = dict_find(iterator, MESSAGE_KEY_DATA_TYPE);
  if(!type_tuple) {
    if(s_history_menu_layer) menu_layer_reload_data(s_history_menu_layer);
    return; 
  }

  int type = type_tuple->value->int32;
  Tuple *index_tuple = dict_find(iterator, MESSAGE_KEY_INDEX);
  int index = index_tuple ? index_tuple->value->int32 : 0;

  if(type == 0) { 
    if(index == 0) free_chat_list();

    Tuple *name_t = dict_find(iterator, MESSAGE_KEY_CHAT_NAME);
    Tuple *preview_t = dict_find(iterator, MESSAGE_KEY_CHAT_PREVIEW);
    Tuple *unread_t = dict_find(iterator, MESSAGE_KEY_UNREAD_COUNT);

    if(name_t && preview_t && index < MAX_CHATS) {
       s_chat_names[index] = malloc(strlen(name_t->value->cstring) + 1);
       strcpy(s_chat_names[index], name_t->value->cstring);

       s_chat_previews[index] = malloc(strlen(preview_t->value->cstring) + 1);
       strcpy(s_chat_previews[index], preview_t->value->cstring);
       
       s_chat_unreads[index] = unread_t ? unread_t->value->int32 : 0;

       s_num_chats = index + 1;
       if(s_menu_layer) menu_layer_reload_data(s_menu_layer);
    }
  } else if(type == 1) {
    if(index == 0) free_chat_history();

    Tuple *sender_t = dict_find(iterator, MESSAGE_KEY_SENDER_NAME);
    Tuple *msg_t = dict_find(iterator, MESSAGE_KEY_MESSAGE_TEXT);
    Tuple *ts_t = dict_find(iterator, MESSAGE_KEY_TIMESTAMP);
    Tuple *receipt_t = dict_find(iterator, MESSAGE_KEY_RECEIPT_STATUS);

    if(sender_t && msg_t && index < MAX_MESSAGES) {
      s_mock_senders[index] = malloc(strlen(sender_t->value->cstring) + 1);
      strcpy(s_mock_senders[index], sender_t->value->cstring);

      s_mock_messages[index] = malloc(strlen(msg_t->value->cstring) + 1);
      strcpy(s_mock_messages[index], msg_t->value->cstring);

      char *time_str = ts_t ? ts_t->value->cstring : "12:00";
      s_mock_timestamps[index] = malloc(strlen(time_str) + 1);
      strcpy(s_mock_timestamps[index], time_str);

      s_mock_receipts[index] = receipt_t ? receipt_t->value->int32 : 0;

      Tuple *reaction_t = dict_find(iterator, MESSAGE_KEY_REACTION);
      if (reaction_t && strlen(reaction_t->value->cstring) > 0) {
        s_mock_reactions[index] = malloc(strlen(reaction_t->value->cstring) + 1);
        strcpy(s_mock_reactions[index], reaction_t->value->cstring);
      } else {
        s_mock_reactions[index] = NULL;
      }

      s_num_messages = index + 1;

      if(s_history_menu_layer) {
        menu_layer_reload_data(s_history_menu_layer);
        // REMOVED: We no longer scroll down here to prevent the "cascading" animation!
      }
    }
  } else if (type == 5) {
    // --- NEW: END OF HISTORY MARKER ---
    // Now we instantly snap to the bottom ONLY when all messages have finished loading!
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

// --- HELPER FUNCTIONS ---
static void request_chat_history(int chat_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    int type = 1; 
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    app_message_outbox_send();
  }
}

static void send_message_to_phone(int chat_id, const char *message) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  
  if(result == APP_MSG_OK) {
    int type = 2; // send text reply
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    dict_write_cstring(iter, MESSAGE_KEY_MESSAGE_TEXT, message);
    app_message_outbox_send();
    
    // Optimistic UI Update!
    if (s_num_messages < MAX_MESSAGES) {
      int idx = s_num_messages;
      s_mock_senders[idx] = malloc(3); strcpy(s_mock_senders[idx], "Me");
      s_mock_messages[idx] = malloc(strlen(message) + 1); strcpy(s_mock_messages[idx], message);
      
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char time_buf[10];
      strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
      s_mock_timestamps[idx] = malloc(strlen(time_buf) + 1);
      strcpy(s_mock_timestamps[idx], time_buf);
      
      s_mock_receipts[idx] = 1; // Artificially inject "Sent" status!

      s_num_messages++;
      
      if(s_history_menu_layer) {
        menu_layer_reload_data(s_history_menu_layer);
        MenuIndex last_idx = get_menu_index_from_msg_idx(s_num_messages - 1);
        menu_layer_set_selected_index(s_history_menu_layer, last_idx, MenuRowAlignBottom, false);
      }
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error preparing outbox: %d", (int)result);
  }
}

// --- NEW: REACTION TO PHONE HELPER ---
static void send_reaction_to_phone(int chat_id, int msg_idx, const char *reaction) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  
  if(result == APP_MSG_OK) {
    int type = 4; // Type 4 = Reaction
    dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &chat_id, sizeof(int), true);
    dict_write_int(iter, MESSAGE_KEY_INDEX, &msg_idx, sizeof(int), true); 
    dict_write_cstring(iter, MESSAGE_KEY_MESSAGE_TEXT, reaction);
    app_message_outbox_send();
    
    // Provide a subtle vibration to confirm the reaction was dispatched!
    vibes_short_pulse();
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error preparing outbox for reaction: %d", (int)result);
  }
}

// --- REACTION MENU CALLBACKS & WINDOW ---
static uint16_t reaction_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return NUM_REACTIONS;
}

static int16_t reaction_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return 44; 
}

static void reaction_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  
  // ---> THE FIX: Smart text contrast for both Color and B&W watches! <---
  #ifdef PBL_COLOR
    // On Color watches, Mint Green (Light) needs Black text, Dark Green (Dark) needs White text.
    graphics_context_set_text_color(ctx, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
    // On B&W watches, highlighting physically inverts the screen colors, so we flip the text.
    if (s_is_dark_mode) {
      graphics_context_set_text_color(ctx, highlighted ? GColorBlack : GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    }
  #endif
  
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

  GRect header_bounds = GRect(0, 0, bounds.size.w, 24);
  s_reaction_header_layer = text_layer_create(header_bounds);
  
  text_layer_set_text(s_reaction_header_layer, "React to Message");
  text_layer_set_font(s_reaction_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_reaction_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_reaction_header_layer, GTextOverflowModeTrailingEllipsis);

  #ifdef PBL_COLOR
  text_layer_set_background_color(s_reaction_header_layer, s_is_dark_mode ? GColorDarkGray : (GColor){.argb = 0x40});
  text_layer_set_text_color(s_reaction_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  text_layer_set_background_color(s_reaction_header_layer, s_is_dark_mode ? GColorBlack : GColorLightGray);
  text_layer_set_text_color(s_reaction_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #endif

  layer_add_child(window_layer, text_layer_get_layer(s_reaction_header_layer));

  GRect menu_bounds = GRect(0, 24, bounds.size.w, bounds.size.h - 24);
  s_reaction_menu_layer = menu_layer_create(menu_bounds);

  #ifdef PBL_COLOR
  menu_layer_set_normal_colors(s_reaction_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_reaction_menu_layer, s_is_dark_mode ? GColorDarkGreen : GColorMintGreen, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  menu_layer_set_normal_colors(s_reaction_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_reaction_menu_layer, s_is_dark_mode ? GColorWhite : GColorBlack, s_is_dark_mode ? GColorBlack : GColorWhite);
  #endif

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


// --- CANNED MENU CALLBACKS & WINDOW ---
static uint16_t canned_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  #ifdef PBL_MICROPHONE
  return NUM_CANNED + 1;
  #else
  return NUM_CANNED;
  #endif
}

static int16_t canned_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return 44; // Give the rows a nice, modern height for tap targets
}

static void canned_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  
  // ---> THE FIX: Smart text contrast for both Color and B&W watches! <---
  #ifdef PBL_COLOR
    // On Color watches, Mint Green (Light) needs Black text, Dark Green (Dark) needs White text.
    graphics_context_set_text_color(ctx, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
    // On B&W watches, highlighting physically inverts the screen colors, so we flip the text.
    if (s_is_dark_mode) {
      graphics_context_set_text_color(ctx, highlighted ? GColorBlack : GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    }
  #endif
  
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

  // Draw custom row text properly vertically centered
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

  // --- PERSISTENT TOP HEADER FOR REPLY MENU ---
  GRect header_bounds = GRect(0, 0, bounds.size.w, 24);
  s_canned_header_layer = text_layer_create(header_bounds);
  
  // Dynamically insert the chat name into the header!
  snprintf(s_reply_header_buffer, sizeof(s_reply_header_buffer), "Reply to %s", s_chat_names[s_selected_chat_id]);
  text_layer_set_text(s_canned_header_layer, s_reply_header_buffer);
  text_layer_set_font(s_canned_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_canned_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_canned_header_layer, GTextOverflowModeTrailingEllipsis);

  // Same Nifty Trick for super light gray background
  #ifdef PBL_COLOR
  text_layer_set_background_color(s_canned_header_layer, s_is_dark_mode ? GColorDarkGray : (GColor){.argb = 0x40});
  text_layer_set_text_color(s_canned_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  text_layer_set_background_color(s_canned_header_layer, s_is_dark_mode ? GColorBlack : GColorLightGray);
  text_layer_set_text_color(s_canned_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #endif

  layer_add_child(window_layer, text_layer_get_layer(s_canned_header_layer));

  // --- REPLY MENU LAYER ---
  GRect menu_bounds = GRect(0, 24, bounds.size.w, bounds.size.h - 24);
  s_canned_menu_layer = menu_layer_create(menu_bounds);

  // Apply the Mint Green highlight
  #ifdef PBL_COLOR
  menu_layer_set_normal_colors(s_canned_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_canned_menu_layer, s_is_dark_mode ? GColorDarkGreen : GColorMintGreen, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  menu_layer_set_normal_colors(s_canned_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_canned_menu_layer, s_is_dark_mode ? GColorWhite : GColorBlack, s_is_dark_mode ? GColorBlack : GColorWhite);
  #endif

  menu_layer_set_callbacks(s_canned_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = canned_get_num_rows_callback,
    .get_cell_height = canned_get_cell_height_callback, // Link new height formatter
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

// --- HISTORY MENU CALLBACKS & WINDOW ---

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

static uint16_t history_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  if (s_num_messages == 0) return 1;
  uint16_t sections = 1;
  for (int i = 1; i < s_num_messages; i++) {
    if (strcmp(s_mock_senders[i], s_mock_senders[i-1]) != 0) sections++;
  }
  // ADD +1 FOR THE VIRTUAL "LOAD MORE" SECTION AT THE TOP
  return sections + 1;
}

static uint16_t history_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  if (s_num_messages == 0) return 1;
  
  // NEW: Section 0 is the "Load More" button!
  if (section_index == 0) return 1; 

  // For all other sections, calculate the true index by subtracting 1
  int real_section = section_index - 1;
  int current_section = 0;
  int count = 1;
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
  if (s_num_messages == 0 || section_index == 0) return 0; // No header for the Load More button

  int real_section = section_index - 1;
  int msg_idx = get_message_index(real_section, 0);
  
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);
  bool is_1on1 = (strcmp(s_mock_senders[msg_idx], s_chat_names[s_selected_chat_id]) == 0);

  if (is_me || is_1on1) {
    return 0; 
  }

  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static int16_t history_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0) return 32;

  // Give the Load More button a nice, touchable height
  if (cell_index->section == 0) return 44;

  int real_section = cell_index->section - 1;
  int msg_idx = get_message_index(real_section, cell_index->row);
  GRect bounds = layer_get_bounds(menu_layer_get_layer(menu_layer));

  int max_text_width = bounds.size.w - 32;
  GRect max_text_bounds = GRect(0, 0, max_text_width, 2000);

  GSize text_size = graphics_text_layout_get_content_size(
    s_mock_messages[msg_idx],
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    max_text_bounds,
    GTextOverflowModeWordWrap,
    GTextAlignmentLeft
  );

  // ---> THE FIX: Add extra height if this message has a reaction! <---
  bool has_reaction = (s_mock_reactions[msg_idx] != NULL);
  int reaction_padding = has_reaction ? 10 : 0;

  // Add the reaction padding to the total calculated height of the row
  int16_t calculated_height = text_size.h + 12 + 16 + 4 + reaction_padding;

  return (calculated_height > 44) ? calculated_height : 44;
}

static void history_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  if (s_num_messages == 0 || section_index == 0) return;
  
  int real_section = section_index - 1;
  int msg_idx = get_message_index(real_section, 0);
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);
  bool is_1on1 = (strcmp(s_mock_senders[msg_idx], s_chat_names[s_selected_chat_id]) == 0);

  if (is_me || is_1on1) return;

  GRect bounds = layer_get_bounds(cell_layer);

  graphics_context_set_fill_color(ctx, s_is_dark_mode ? GColorBlack : GColorWhite);
  graphics_context_set_text_color(ctx, s_is_dark_mode ? GColorWhite : GColorBlack);

  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_draw_text(ctx, s_mock_senders[msg_idx],
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     grect_inset(bounds, GEdgeInsets(2, 4, 2, 4)), 
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Helper to convert the safe Bluetooth string back to a hardcoded C emoji
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

static void history_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);

  graphics_context_set_fill_color(ctx, s_is_dark_mode ? GColorBlack : GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_num_messages == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "Loading messages...", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // --- NEW: DRAW THE LOAD MORE BUTTON ---
  if (cell_index->section == 0) {
    bool highlighted = menu_cell_layer_is_highlighted(cell_layer);

    #ifdef PBL_COLOR
    // Use Mint Green when selected to match the main menu, otherwise Light Gray
    graphics_context_set_fill_color(ctx, highlighted ? GColorMintGreen : GColorLightGray);
    graphics_context_set_text_color(ctx, GColorBlack);
    #else
    graphics_context_set_fill_color(ctx, highlighted ? GColorBlack : GColorLightGray);
    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    #endif

    // Draw a nice rounded pill button
    graphics_fill_rect(ctx, grect_inset(bounds, GEdgeInsets(6, 16, 6, 16)), 16, GCornersAll);

    graphics_draw_text(ctx, "Load older messages...", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(0, 12, bounds.size.w, 20),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  int real_section = cell_index->section - 1;
  int msg_idx = get_message_index(real_section, cell_index->row);
  bool is_me = (strcmp(s_mock_senders[msg_idx], "Me") == 0);
  
  MenuIndex selected_index = menu_layer_get_selected_index(s_history_menu_layer);
  bool highlighted = (cell_index->section == selected_index.section && cell_index->row == selected_index.row);

  // Bubble Layout Configuration
  int margin_x = 4;
  int margin_y = 2;
  int padding_x = 8;
  int padding_y = 6;
  int timestamp_h = 16;
  int max_text_width = bounds.size.w - 32;

  GRect max_text_bounds = GRect(0, 0, max_text_width, 2000);
  GSize text_size = graphics_text_layout_get_content_size(
    s_mock_messages[msg_idx],
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    max_text_bounds,
    GTextOverflowModeWordWrap,
    GTextAlignmentLeft
  );

  bool has_reaction = (s_mock_reactions[msg_idx] != NULL);
  int reaction_padding = has_reaction ? 10 : 0;

  int bubble_w = text_size.w + (padding_x * 2);
  if (bubble_w < 60) bubble_w = 60; 
  int bubble_h = text_size.h + (padding_y * 2) + timestamp_h;

  int bubble_x = is_me ? (bounds.size.w - bubble_w - margin_x) : margin_x;
  int bubble_y = margin_y;

  GRect bubble_rect = GRect(bubble_x, bubble_y, bubble_w, bubble_h);

  GColor base_color;
  GColor bubble_color;
  GColor text_color;
  GColor time_color;

  #ifdef PBL_COLOR
    base_color = is_me ? s_color_me : s_color_other;
    
    if (highlighted) {
      bubble_color = get_highlight_shade(base_color);
    } else {
      bubble_color = base_color;
    }
    
    text_color = gcolor_legible_over(bubble_color);
    time_color = gcolor_equal(text_color, GColorWhite) ? GColorLightGray : GColorDarkGray;
  #else
    if (highlighted) {
      bubble_color = GColorBlack;
      text_color = GColorWhite;
      time_color = GColorWhite;
    } else {
      bubble_color = is_me ? GColorWhite : GColorLightGray;
      text_color = GColorBlack;
      time_color = GColorBlack;
    }
  #endif

  // 5. Draw Bubble
  graphics_context_set_fill_color(ctx, bubble_color);
  graphics_fill_rect(ctx, bubble_rect, 8, GCornersAll);

  // ---> NEW: DRAW THE MESSAGE TAIL! <---
  // Only draw the tail if this is the FIRST message in a sequence from the same sender
  if (cell_index->row == 0) {
    graphics_context_set_stroke_color(ctx, bubble_color);

    int tail_h = 10; // Height of the tail
    int tail_w = 4; // How far it sticks out

    if (is_me) {
      // Draw the tail on the top-right corner, pointing right
      int start_x = bubble_x + bubble_w - 6; // Start slightly inside to prevent gaps
      for (int i = 0; i < tail_h; i++) {
        int end_x = bubble_x + bubble_w + tail_w - i;
        graphics_draw_line(ctx, GPoint(start_x, bubble_y + i), GPoint(end_x, bubble_y + i));
      }
    } else {
      // Draw the tail on the top-left corner, pointing left
      int start_x = bubble_x + 6; // Start slightly inside
      for (int i = 0; i < tail_h; i++) {
        int end_x = bubble_x - tail_w + i;
        graphics_draw_line(ctx, GPoint(end_x, bubble_y + i), GPoint(start_x, bubble_y + i));
      }
    }
  }

  #ifndef PBL_COLOR
    if (!highlighted && is_me) {
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_round_rect(ctx, bubble_rect, 8);
    }
  #endif

  // 6. Draw the Message Text
  GRect text_rect = GRect(bubble_x + padding_x, bubble_y + padding_y - 2, bubble_w - (padding_x * 2), text_size.h);
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx, s_mock_messages[msg_idx],
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     text_rect, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // 7. Draw the Timestamp
  int check_space = (is_me && s_mock_receipts[msg_idx] > 0) ? 14 : 0;
  GRect time_rect = GRect(bubble_x + padding_x, text_rect.origin.y + text_size.h, bubble_w - (padding_x * 2) - check_space, timestamp_h);
  graphics_context_set_text_color(ctx, time_color);
  graphics_draw_text(ctx, s_mock_timestamps[msg_idx],
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     time_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ---> 8. DRAW READ RECEIPTS! <---
  if (is_me && s_mock_receipts[msg_idx] > 0) {
    int receipt = s_mock_receipts[msg_idx];
    
    GColor check_color = time_color;
    #ifdef PBL_COLOR
      if (receipt >= 3) {
        check_color = gcolor_equal(text_color, GColorBlack) ? GColorBlue : GColorCyan;
      }
    #endif

    graphics_context_set_stroke_color(ctx, check_color);
    
    int cx = bubble_x + bubble_w - padding_x - 12;
    int cy = time_rect.origin.y + 7; 

    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(cx + 2, cy + 3));
    graphics_draw_line(ctx, GPoint(cx + 2, cy + 3), GPoint(cx + 6, cy - 3));
    
    if (receipt >= 2) {
      int cx2 = cx + 5;
      graphics_draw_line(ctx, GPoint(cx2, cy), GPoint(cx2 + 2, cy + 3));
      graphics_draw_line(ctx, GPoint(cx2 + 2, cy + 3), GPoint(cx2 + 6, cy - 3));
    }
  }

  // ---> 9. DRAW THE SEPARATE REACTION BADGE! <---
  if (has_reaction) {
    // Translate the safe Bluetooth string back into a pixel-art emoji!
    char* display_emoji = get_emoji_for_reaction(s_mock_reactions[msg_idx]);

    // Position it at the bottom-RIGHT corner of the message bubble
    int rw = 28;
    int rh = 18;
    int rx = bubble_x + bubble_w - rw + 4;
    int ry = bubble_rect.origin.y + bubble_rect.size.h - 8;

    GRect reaction_rect = GRect(rx, ry, rw, rh);

    // 1. Draw the badge background (Solid Fill with Easter Egg!)
    #ifdef PBL_COLOR
    if (strcmp(display_emoji, "❤️") == 0) {
      graphics_context_set_fill_color(ctx, GColorMelon); // Soft red for the heart!
    } else {
      graphics_context_set_fill_color(ctx, GColorPastelYellow); // Yellow for smileys!
    }
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_rect(ctx, reaction_rect, 9, GCornersAll);

    // 2. Draw a subtle outline
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #else
    graphics_context_set_stroke_color(ctx, GColorBlack);
    #endif
    graphics_draw_round_rect(ctx, reaction_rect, 9);

    // 3. Draw the emoji lines in crisp black!
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, display_emoji,
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(rx, ry - 7, rw, rh + 7),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void history_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0) return; 

  // --- NEW: INTERCEPT "LOAD MORE" CLICKS ---
  if (cell_index->section == 0) {
    s_auto_scroll_to_bottom = false; // Disable scroll jumping!
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      int type = 3; // Type 3 = Trigger Pagination fetch
      dict_write_int(iter, MESSAGE_KEY_DATA_TYPE, &type, sizeof(int), true);
      dict_write_int(iter, MESSAGE_KEY_CHAT_ID, &s_selected_chat_id, sizeof(int), true);
      app_message_outbox_send();
    }
    return; // Exit out before opening the reply menu!
  }

  if(!s_canned_window) {
    s_canned_window = window_create();
    window_set_window_handlers(s_canned_window, (WindowHandlers) {
      .load = canned_window_load,
      .unload = canned_window_unload
    });
  }
  window_stack_push(s_canned_window, true);
}

// --- NEW: LONG PRESS TO REACT ---
static void history_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_messages == 0) return;
  
  // Can't react to the "Load More" button!
  if (cell_index->section == 0) return;

  // Calculate exact message index from the list
  int real_section = cell_index->section - 1;
  s_selected_message_idx = get_message_index(real_section, cell_index->row);

  // Push the Reaction Menu Window
  if(!s_reaction_window) {
    s_reaction_window = window_create();
    window_set_window_handlers(s_reaction_window, (WindowHandlers) {
      .load = reaction_window_load,
      .unload = reaction_window_unload
    });
  }
  window_stack_push(s_reaction_window, true);
}

static void history_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  GRect header_bounds = GRect(0, 0, bounds.size.w, 24);
  s_history_header_layer = text_layer_create(header_bounds);
  
  text_layer_set_text(s_history_header_layer, s_chat_names[s_selected_chat_id]);
  text_layer_set_font(s_history_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_history_header_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_history_header_layer, GTextOverflowModeTrailingEllipsis);

  // --- THE NIFTY TRICK: Super Light Gray ---
  #ifdef PBL_COLOR
  text_layer_set_background_color(s_history_header_layer, s_is_dark_mode ? GColorDarkGray : (GColor){.argb = 0x40});
  text_layer_set_text_color(s_history_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  text_layer_set_background_color(s_history_header_layer, s_is_dark_mode ? GColorBlack : GColorLightGray);
  text_layer_set_text_color(s_history_header_layer, s_is_dark_mode ? GColorWhite : GColorBlack);
  #endif

  layer_add_child(window_layer, text_layer_get_layer(s_history_header_layer));

  GRect menu_bounds = GRect(0, 24, bounds.size.w, bounds.size.h - 24);
  s_history_menu_layer = menu_layer_create(menu_bounds);
  
  menu_layer_set_normal_colors(s_history_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_history_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);

  menu_layer_set_callbacks(s_history_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = history_get_num_sections_callback,
    .get_header_height = history_get_header_height_callback,
    .draw_header = history_draw_header_callback,
    .get_num_rows = history_get_num_rows_callback,
    .get_cell_height = history_get_cell_height_callback, 
    .draw_row = history_draw_row_callback,
    .select_click = history_select_callback,
    .select_long_click = history_select_long_callback, // REGISTER LONG CLICK
  });

  menu_layer_set_click_config_onto_window(s_history_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_history_menu_layer));
}

static void history_window_unload(Window *window) {
  menu_layer_destroy(s_history_menu_layer);
  text_layer_destroy(s_history_header_layer); 
}

// --- DICTATION CALLBACKS ---
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

// --- MAIN MENU LAYER CALLBACKS (POLISHED) ---
static int16_t menu_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_chats == 0) {
    return layer_get_bounds(menu_layer_get_layer(menu_layer)).size.h;
  }
  return 60;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_chats == 0 ? 1 : s_num_chats;
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  (void)highlighted; // FIX: Silence unused variable warning on Color watches

  if (s_num_chats == 0) {
    // --- FORCE WHITE BACKGROUND FOR LOADING SCREEN ---
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    int16_t mid_y = bounds.size.h / 2;
    
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorDarkGreen);
    #else
    graphics_context_set_text_color(ctx, GColorBlack);
    #endif
    
    graphics_draw_text(ctx, "Loading Chats...", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                       GRect(0, mid_y - 28, bounds.size.w, 28), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
                       
    graphics_draw_text(ctx, "Please wait", fonts_get_system_font(FONT_KEY_GOTHIC_18), 
                       GRect(0, mid_y, bounds.size.w, 20), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // --- MODERN CUSTOM DRAWN CHAT ROWS ---
  GColor title_color;
  GColor preview_color;

  #ifdef PBL_COLOR
  title_color = s_is_dark_mode ? GColorWhite : GColorBlack;
  preview_color = s_is_dark_mode ? GColorLightGray : GColorDarkGray;
  #else
  title_color = highlighted ? GColorWhite : (s_is_dark_mode ? GColorWhite : GColorBlack);
  preview_color = highlighted ? GColorWhite : (s_is_dark_mode ? GColorLightGray : GColorBlack);
  #endif

  // --- NEW: UNREAD BADGE LOGIC ---
  int text_width = bounds.size.w - 16;
  int unread_count = s_chat_unreads[cell_index->row];

  if (unread_count > 0) {
    int badge_radius = 11;
    int badge_x = bounds.size.w - badge_radius - 8;
    int badge_y = bounds.size.h / 2;
    
    // Shrink the maximum text width so long chat names don't overlap the badge
    text_width -= (badge_radius * 2 + 8);

    #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorKellyGreen); 
      graphics_context_set_text_color(ctx, GColorWhite);
    #else
      graphics_context_set_fill_color(ctx, highlighted ? GColorWhite : GColorBlack);
      graphics_context_set_text_color(ctx, highlighted ? GColorBlack : GColorWhite);
    #endif
    
    // Draw the perfect circle
    graphics_fill_circle(ctx, GPoint(badge_x, badge_y), badge_radius);
    
    // Convert integer to string (cap at 99+ to prevent text overflowing the circle)
    char unread_str[4];
    snprintf(unread_str, sizeof(unread_str), "%d", unread_count > 99 ? 99 : unread_count);
    
    // Draw the number perfectly centered inside the badge
    graphics_draw_text(ctx, unread_str, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), 
                       GRect(badge_x - 10, badge_y - 10, 20, 20), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Draw the bold Chat Name
  graphics_context_set_text_color(ctx, title_color);
  graphics_draw_text(ctx, s_chat_names[cell_index->row], 
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                     GRect(8, 4, text_width, 28), 
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Draw the subtle Message Preview directly underneath
  graphics_context_set_text_color(ctx, preview_color);
  graphics_draw_text(ctx, s_chat_previews[cell_index->row], 
                     fonts_get_system_font(FONT_KEY_GOTHIC_18), 
                     GRect(8, 32, text_width, 24), 
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_num_chats == 0) return; 

  s_selected_chat_id = cell_index->row;

  s_auto_scroll_to_bottom = true; // Ensure we snap to the newest message on initial open!

  // --- NEW: Instantly clear the unread badge locally for a fast UI! ---
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

// --- WINDOW LIFECYCLE ---
static void main_window_load(Window *window) {
  window_set_background_color(window, s_is_dark_mode ? GColorBlack : GColorWhite);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  // --- PERSISTENT TOP HEADER FOR MAIN MENU ---
  GRect header_bounds = GRect(0, 0, bounds.size.w, 24);
  s_main_header_layer = text_layer_create(header_bounds);
  
  text_layer_set_text(s_main_header_layer, "WhatsApp");
  text_layer_set_font(s_main_header_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_main_header_layer, GTextAlignmentCenter);

  // --- THE NIFTY TRICK: Super Light Gray ---
  #ifdef PBL_COLOR
  // 0x40 is an Alpha 1 (33% opacity) Black. On a white window, this hardware-dithers into a gorgeous super-light gray!
  text_layer_set_background_color(s_main_header_layer, (GColor){.argb = 0x40});
  text_layer_set_text_color(s_main_header_layer, GColorBlack);
  #else
  text_layer_set_background_color(s_main_header_layer, GColorLightGray);
  text_layer_set_text_color(s_main_header_layer, GColorBlack);
  #endif

  layer_add_child(window_layer, text_layer_get_layer(s_main_header_layer));

  // Shift the menu down to sit under the header
  GRect menu_bounds = GRect(0, 24, bounds.size.w, bounds.size.h - 24);
  s_menu_layer = menu_layer_create(menu_bounds);
  
  // --- NEW: Softer Mint Green Highlight! ---
  #ifdef PBL_COLOR
  menu_layer_set_normal_colors(s_menu_layer, s_is_dark_mode ? GColorBlack : GColorWhite, s_is_dark_mode ? GColorWhite : GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, s_is_dark_mode ? GColorDarkGreen : GColorMintGreen, s_is_dark_mode ? GColorWhite : GColorBlack);
  #else
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorBlack, GColorWhite);
  #endif

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

// --- APP LIFECYCLE ---
static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  if (persist_exists(MESSAGE_KEY_REPLY_1)) {
    persist_read_string(MESSAGE_KEY_REPLY_1, reply_1_buffer, sizeof(reply_1_buffer));
  }
  if (persist_exists(MESSAGE_KEY_REPLY_2)) {
    persist_read_string(MESSAGE_KEY_REPLY_2, reply_2_buffer, sizeof(reply_2_buffer));
  }
  if (persist_exists(MESSAGE_KEY_REPLY_3)) {
    persist_read_string(MESSAGE_KEY_REPLY_3, reply_3_buffer, sizeof(reply_3_buffer));
  }
  if (persist_exists(MESSAGE_KEY_REPLY_4)) {
    persist_read_string(MESSAGE_KEY_REPLY_4, reply_4_buffer, sizeof(reply_4_buffer));
  }
  if (persist_exists(MESSAGE_KEY_REPLY_5)) {
    persist_read_string(MESSAGE_KEY_REPLY_5, reply_5_buffer, sizeof(reply_5_buffer));
  }
  if (persist_exists(MESSAGE_KEY_REPLY_6)) {
    persist_read_string(MESSAGE_KEY_REPLY_6, reply_6_buffer, sizeof(reply_6_buffer));
  }
  if (persist_exists(MESSAGE_KEY_DARK_MODE)) {
    s_is_dark_mode = persist_read_bool(MESSAGE_KEY_DARK_MODE);
  }

  // Load custom Theme Colors!
  #ifdef PBL_COLOR
  if (persist_exists(MESSAGE_KEY_THEME_ME)) {
    s_color_me = GColorFromHEX(persist_read_int(MESSAGE_KEY_THEME_ME));
  } else {
    s_color_me = GColorKellyGreen; 
  }
  
  if (persist_exists(MESSAGE_KEY_THEME_OTHER)) {
    s_color_other = GColorFromHEX(persist_read_int(MESSAGE_KEY_THEME_OTHER));
  } else {
    s_color_other = GColorLightGray; 
  }
  #endif

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(1024, 1024);

  window_stack_push(s_main_window, true);
}

static void deinit() {
  window_destroy(s_main_window);
  if(s_canned_window) window_destroy(s_canned_window);
  if(s_reaction_window) window_destroy(s_reaction_window); // CLEANUP REACTION WINDOW
  if(s_history_window) window_destroy(s_history_window);

  free_chat_list();
  free_chat_history();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
