var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// --- GLOBAL STATE ---
var BASE_URL = localStorage.getItem('ServerURL') || 'http://192.168.0.30:3001';
var currentHistoryLimit = parseInt(localStorage.getItem('MessageLimit')) || 10;

// Semantic mapping for AppMessage types
var MSG_TYPE = {
  CHAT_LIST: 0,
  HISTORY: 1,
  REPLY: 2,
  PAGINATE: 3,
  REACTION: 4,
  END_HISTORY: 5,
  DELETE: 6
};

// --- CORE PEBBLEKIT JS LOGIC ---
Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS is ready! Using URL: ' + BASE_URL);
  fetchChatList();
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  var type = dict['DATA_TYPE'];
  var chatId = dict['CHAT_ID'];
  var msgIndex = dict['INDEX'];

  switch (type) {
    case MSG_TYPE.CHAT_LIST:
      console.log("Watch requested chat list...");
      fetchChatList();
      break;

    case MSG_TYPE.HISTORY:
      console.log("Sending history for chat " + chatId);
      currentHistoryLimit = parseInt(localStorage.getItem('MessageLimit')) || 10;
      fetchChatHistory(chatId);
      break;

    case MSG_TYPE.REPLY:
      var replyIndex = msgIndex !== undefined ? msgIndex : -1;
      var limitWhenReplied = currentHistoryLimit;

      if (replyIndex !== -1) {
        console.log("User QUOTE replied to chat " + chatId + " msg index " + replyIndex);
      } else {
        console.log("User sent normal reply to chat " + chatId);
      }

      currentHistoryLimit++; // Optimistically account for the new message
      postReplyToBackend(chatId, dict['MESSAGE_TEXT'], replyIndex, limitWhenReplied);
      break;

    case MSG_TYPE.PAGINATE:
      currentHistoryLimit += 10;
      console.log("Pagination triggered! Fetching " + currentHistoryLimit + " messages for chat " + chatId);
      fetchChatHistory(chatId);
      break;

    case MSG_TYPE.REACTION:
      console.log("User reacted to msg index " + msgIndex + " in chat " + chatId);
      postReactionToBackend(chatId, msgIndex, dict['MESSAGE_TEXT']);
      break;

    case MSG_TYPE.DELETE:
      console.log("User requested to delete msg index " + msgIndex + " in chat " + chatId);
      deleteMessageInBackend(chatId, msgIndex);
      break;

    default:
      console.log("Unknown AppMessage type received: " + type);
  }
});


// --- CLAY & SETTINGS LOGIC ---
Pebble.addEventListener('showConfiguration', function() {
  var url = clay.generateUrl();
  var host = BASE_URL.replace(/^https?:\/\//, '');
  // Steam Deck KIO Fix
  url = url.replace('clay.pebble.com.s3-website-us-west-2.amazonaws.com/', host + '/clay');
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;

  var configData = JSON.parse(decodeURIComponent(e.response));
  var needsRefetch = false;

  // Helper to safely extract values and detect if network-dependent settings changed
  function updateSetting(key, configObj, triggersRefetch) {
    if (configObj && configObj.value !== undefined) {
      var val = String(configObj.value);
      if (triggersRefetch && localStorage.getItem(key) !== val) needsRefetch = true;
      localStorage.setItem(key, val);
      return val;
    }
    return localStorage.getItem(key);
  }

  function parseColor(val, defaultHex) {
    if (val === undefined || val === null) return defaultHex;
    if (typeof val === 'number') return val;
    if (typeof val === 'string') return parseInt(val.replace(/^#|0x/, ''), 16);
    return defaultHex;
  }

  // Parse UI/App config
  BASE_URL = updateSetting('ServerURL', configData['ServerURL'], true) || BASE_URL;
  updateSetting('ChatLimit', configData['ChatLimit'], true);
  updateSetting('MessageLimit', configData['MessageLimit'], false);
  updateSetting('HideGroupChats', configData['HideGroupChats'], true);
  updateSetting('UnreadOnly', configData['UnreadOnly'], true);
  updateSetting('DarkMode', configData['DarkMode'], true);

  // Update Custom Replies
  for (var i = 1; i <= 6; i++) {
    updateSetting('Reply' + i, configData['Reply' + i], false);
  }

  // Package dictionary for the watch
  var replyDict = {
    'DARK_MODE': localStorage.getItem('DarkMode') === 'true' ? 1 : 0,
                        'THEME_ME': parseColor(configData['THEME_ME'] ? configData['THEME_ME'].value : null, 0x00AA55),
                        'THEME_OTHER': parseColor(configData['THEME_OTHER'] ? configData['THEME_OTHER'].value : null, 0xAAAAAA),
                        'REPLY_1': localStorage.getItem('Reply1') || 'Yes',
                        'REPLY_2': localStorage.getItem('Reply2') || 'No',
                        'REPLY_3': localStorage.getItem('Reply3') || 'I\'ll call you later',
                        'REPLY_4': localStorage.getItem('Reply4') || 'OK',
                        'REPLY_5': localStorage.getItem('Reply5') || 'On my way!',
                        'REPLY_6': localStorage.getItem('Reply6') || 'Can\'t talk right now.'
  };

  Pebble.sendAppMessage(replyDict,
                        function() { console.log("Config sent to watch successfully!"); },
                        function(err) { console.log("Error sending config to watch: " + JSON.stringify(err)); }
  );

  if (needsRefetch && BASE_URL) {
    console.log("Network settings changed. Refetching chat list...");
    fetchChatList();
  }
});


// --- NETWORK HELPERS ---

// Universal XHR Wrapper to prevent duplicate code and handle memory efficiently
function apiRequest(method, endpoint, payload, onSuccess) {
  var url = BASE_URL + endpoint;
  var req = new XMLHttpRequest();
  req.open(method, url, true);
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  if (payload) {
    req.setRequestHeader('Content-Type', 'application/json');
  }

  req.onload = function() {
    if (req.readyState === 4) {
      if (req.status === 200) {
        var response = req.responseText ? JSON.parse(req.responseText) : null;
        if (onSuccess) onSuccess(response);
      } else {
        console.log("API Error [" + method + " " + endpoint + "]: HTTP " + req.status);
      }
    }
  };

  req.onerror = function() {
    console.log("Network error attempting to reach " + url);
  };

  req.send(payload ? JSON.stringify(payload) : null);
}

// Queue AppMessages recursively to prevent flooding the Bluetooth buffer
function sendMessages(messageArray) {
  function sendNext(index) {
    if (index >= messageArray.length) return;
    Pebble.sendAppMessage(messageArray[index],
                          function() { sendNext(index + 1); },
                          function(e) { console.log("Failed to send message via BT: " + JSON.stringify(e)); }
    );
  }
  sendNext(0);
}


// --- API EXECUTIONS ---

function fetchChatList() {
  var chatLimit = localStorage.getItem('ChatLimit') || 8;
  var hideGroups = localStorage.getItem('HideGroupChats') === 'true';
  var unreadOnly = localStorage.getItem('UnreadOnly') === 'true';

  var query = '?limit=' + chatLimit + (hideGroups ? '&hideGroups=true' : '') + (unreadOnly ? '&unreadOnly=true' : '');
  console.log(">>> Fetching Chat List...");

  apiRequest('GET', '/api/chats' + query, null, function(response) {
    var chats = [];
    if (response.length === 0) {
      chats.push({
        'DATA_TYPE': MSG_TYPE.CHAT_LIST,
        'INDEX': 0,
        'CHAT_NAME': "All caught up!",
        'CHAT_PREVIEW': "No unread messages found."
      });
    } else {
      for (var i = 0; i < response.length; i++) {
        chats.push({
          'DATA_TYPE': MSG_TYPE.CHAT_LIST,
          'INDEX': i,
          'CHAT_NAME': response[i].name,
          'CHAT_PREVIEW': response[i].preview,
          'UNREAD_COUNT': response[i].unreadCount || 0
        });
      }
    }
    sendMessages(chats);
  });
}

function fetchChatHistory(chatId) {
  apiRequest('GET', '/api/chats/' + chatId + '/messages?limit=' + currentHistoryLimit, null, function(response) {
    var history = [];

    if (response.length === 0) {
      history.push({
        'DATA_TYPE': MSG_TYPE.HISTORY,
        'INDEX': 0,
        'SENDER_NAME': 'System',
        'TIMESTAMP': '',
        'MESSAGE_TEXT': 'No messages yet.',
        'RECEIPT_STATUS': 0
      });
    } else {
      for (var i = 0; i < response.length; i++) {
        var msgPayload = {
          'DATA_TYPE': MSG_TYPE.HISTORY,
          'INDEX': i,
          'SENDER_NAME': response[i].sender,
          'TIMESTAMP': response[i].timestamp,
          'MESSAGE_TEXT': response[i].text,
          'RECEIPT_STATUS': response[i].receipt || 0
        };

        if (response[i].reaction && response[i].reaction.length > 0) {
          msgPayload['REACTION'] = response[i].reaction;
        }
        history.push(msgPayload);
      }
    }

    history.push({ 'DATA_TYPE': MSG_TYPE.END_HISTORY });
    sendMessages(history);
  });
}

function postReplyToBackend(chatId, messageText, msgIndex, originalLimit) {
  var payload = { text: messageText };
  if (msgIndex !== undefined && msgIndex !== -1) {
    payload.msgIndex = msgIndex;
    payload.limit = originalLimit;
  }

  apiRequest('POST', '/api/chats/' + chatId + '/messages', payload, function() {
    console.log("Message successfully delivered!");
    fetchChatHistory(chatId); // Force sync
  });
}

function deleteMessageInBackend(chatId, msgIndex) {
  var payload = { msgIndex: msgIndex, limit: currentHistoryLimit };

  apiRequest('POST', '/api/chats/' + chatId + '/delete', payload, function() {
    console.log("Delete command successfully delivered!");
    fetchChatHistory(chatId); // Force sync
  });
}

function postReactionToBackend(chatId, msgIndex, reactionText) {
  var payload = { msgIndex: msgIndex, limit: currentHistoryLimit, reaction: reactionText };

  apiRequest('POST', '/api/chats/' + chatId + '/react', payload, function() {
    console.log("Reaction successfully delivered!");
  });
}
