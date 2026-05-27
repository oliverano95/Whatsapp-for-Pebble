// --- CLAY INTEGRATION ---
var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Initialize Clay, but turn OFF auto-handling so we don't send the URL to the watch!
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var BASE_URL = 'http://192.168.0.30:3001'; // Default fallback
var currentHistoryLimit = 10; // Track the pagination limit for reactions!

Pebble.addEventListener('showConfiguration', function(e) {
  var url = clay.generateUrl();

  // ---> THE STEAM DECK KIO FIX <---
  // KDE Plasma / KIO cannot handle data: URIs, so we route the config
  // page directly through your own local Node.js backend.
  // We dynamically use whatever IP you saved in your settings!
  var host = BASE_URL.replace(/^https?:\/\//, '');
  url = url.replace(
    'clay.pebble.com.s3-website-us-west-2.amazonaws.com/',
    host + '/clay'
  );

  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) { return; }

  var decodedResponse = decodeURIComponent(e.response);
  var configData = JSON.parse(decodedResponse);

  var flatSettings = {};
  Object.keys(configData).forEach(function(key) {
    if (configData[key] && configData[key].value !== undefined) {
      flatSettings[key] = configData[key].value;
    }
  });

  localStorage.setItem('clay-settings', JSON.stringify(flatSettings));
  console.log("Raw response from Clay: " + decodedResponse);

  var needsRefetch = false;

  // --- 1. Handle Server URL ---
  var newUrl = configData['ServerURL'];
  if (newUrl && newUrl.value !== undefined) {
    if (localStorage.getItem('ServerURL') !== newUrl.value) needsRefetch = true;
    BASE_URL = newUrl.value;
    console.log("Settings updated! New BASE_URL is: " + BASE_URL);
    localStorage.setItem('ServerURL', BASE_URL);
  }

  // --- 2. Handle Chat Limit ---
  var newChatLimit = configData['ChatLimit'];
  if (newChatLimit && newChatLimit.value !== undefined) {
    if (localStorage.getItem('ChatLimit') !== String(newChatLimit.value)) needsRefetch = true;
    console.log("New Chat Limit is: " + newChatLimit.value);
    localStorage.setItem('ChatLimit', newChatLimit.value);
  }

  // --- 3. Handle Message Limit ---
  var newMsgLimit = configData['MessageLimit'];
  if (newMsgLimit && newMsgLimit.value !== undefined) {
    console.log("New Message Limit is: " + newMsgLimit.value);
    localStorage.setItem('MessageLimit', newMsgLimit.value);
  }

  // --- 4. Handle Filters ---
  var hideGroups = configData['HideGroupChats'];
  if (hideGroups !== undefined && hideGroups.value !== undefined) {
    if (localStorage.getItem('HideGroupChats') !== String(hideGroups.value)) needsRefetch = true;
    console.log("Hide Group Chats is: " + hideGroups.value);
    localStorage.setItem('HideGroupChats', String(hideGroups.value));
  }

  var unreadOnly = configData['UnreadOnly'];
  if (unreadOnly !== undefined && unreadOnly.value !== undefined) {
    if (localStorage.getItem('UnreadOnly') !== String(unreadOnly.value)) needsRefetch = true;
    console.log("Show Unread Only is: " + unreadOnly.value);
    localStorage.setItem('UnreadOnly', String(unreadOnly.value));
  }

  // --- Handle Dark Mode ---
  var isDark = configData['DarkMode'];
  if (isDark !== undefined && isDark.value !== undefined) {
    if (localStorage.getItem('DarkMode') !== String(isDark.value)) needsRefetch = true;
    console.log("Dark Mode is: " + isDark.value);
    localStorage.setItem('DarkMode', String(isDark.value));
  }

  // --- 5. Handle Custom Replies ---
  var r1 = configData['Reply1'];
  var r2 = configData['Reply2'];
  var r3 = configData['Reply3'];
  var r4 = configData['Reply4'];
  var r5 = configData['Reply5'];
  var r6 = configData['Reply6'];

  if (r1 && r1.value !== undefined) localStorage.setItem('Reply1', r1.value);
  if (r2 && r2.value !== undefined) localStorage.setItem('Reply2', r2.value);
  if (r3 && r3.value !== undefined) localStorage.setItem('Reply3', r3.value);
  if (r4 && r4.value !== undefined) localStorage.setItem('Reply4', r4.value);
  if (r5 && r5.value !== undefined) localStorage.setItem('Reply5', r5.value);
  if (r6 && r6.value !== undefined) localStorage.setItem('Reply6', r6.value);

  // --- NEW: Bulletproof Color Parser ---
  function parseColor(val, defaultHex) {
    if (val === undefined || val === null) return defaultHex;
    if (typeof val === 'number') return val;
    if (typeof val === 'string') {
      return parseInt(val.replace(/^#|0x/, ''), 16);
    }
    return defaultHex;
  }

  // Package the replies and themes to send to the C code on the watch
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

  // Send the dictionary to the watch
  Pebble.sendAppMessage(replyDict, function() {
    console.log("Custom replies and themes sent to watch successfully!");
  }, function(e) {
    console.log("Error sending replies to watch: " + JSON.stringify(e));
  });

  if (needsRefetch && BASE_URL) {
    console.log("Network-dependent settings changed. Refetching chat list...");
    fetchChatList();
  } else {
    console.log("Only UI/Reply settings changed. Skipping chat list refetch to save battery!");
  }
});

// --- CORE PEBBLEKIT JS LOGIC ---
Pebble.addEventListener('ready', function(e) {
  var savedURL = localStorage.getItem('ServerURL');
  if (savedURL) {
    BASE_URL = savedURL;
  }

  console.log('PebbleKit JS is ready! Using URL: ' + BASE_URL);
  fetchChatList();
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  var type = dict['DATA_TYPE'];

  if (type === 0) {
    console.log("Watch requested chat list...");
    fetchChatList();

  } else if (type === 1) {
    var chatId = dict['CHAT_ID'];
    console.log("Sending history for chat " + chatId);

    // ---> THE FIX: Reset the limit back to the user's default when opening a new chat! <---
    currentHistoryLimit = parseInt(localStorage.getItem('MessageLimit')) || 10;

    fetchChatHistory(chatId);

  } else if (type === 2) {
    var replyIndex = dict['INDEX'] !== undefined ? dict['INDEX'] : -1;

    // Save the limit exactly as it was when the user clicked "Reply"
    var limitWhenReplied = currentHistoryLimit;

    if (replyIndex !== -1) {
      console.log("User QUOTE replied to chat " + dict['CHAT_ID'] + " msg index " + replyIndex + " with: " + dict['MESSAGE_TEXT']);
    } else {
      console.log("User sent normal reply to chat " + dict['CHAT_ID'] + " with message: " + dict['MESSAGE_TEXT']);
    }

    // ---> THE FIX: Increment the limit tracking to account for the new message! <---
    currentHistoryLimit++;

    // Pass all the data to the backend function
    postReplyToBackend(dict['CHAT_ID'], dict['MESSAGE_TEXT'], replyIndex, limitWhenReplied);

  } else if (type === 3) { // PAGINATION
    currentHistoryLimit += 10;
    console.log("Pagination triggered! Fetching " + currentHistoryLimit + " messages for chat " + dict['CHAT_ID']);
    fetchChatHistory(dict['CHAT_ID']);

  } else if (type === 4) { // REACTION
    console.log("User reacted to chat " + dict['CHAT_ID'] + " msg index " + dict['INDEX'] + " with: " + dict['MESSAGE_TEXT']);
    postReactionToBackend(dict['CHAT_ID'], dict['INDEX'], dict['MESSAGE_TEXT']);

    // ---> THE FIX: Catch the Delete command! <---
  } else if (type === 6) { // DELETE
    console.log("User requested to delete msg index " + dict['INDEX'] + " in chat " + dict['CHAT_ID']);
    deleteMessageInBackend(dict['CHAT_ID'], dict['INDEX']);
  }
});

// --- HELPER: Queue AppMessages ---
function sendMessages(messageArray) {
  function sendNext(index) {
    if (index >= messageArray.length) return;

    Pebble.sendAppMessage(messageArray[index], function() {
      sendNext(index + 1);
    }, function(e) {
      console.log("Failed to send message: " + JSON.stringify(e));
    });
  }
  sendNext(0);
}

// --- API CALLS ---
function fetchChatList() {
  var chatLimit = localStorage.getItem('ChatLimit') || 8;
  var hideGroups = localStorage.getItem('HideGroupChats');
  var unreadOnly = localStorage.getItem('UnreadOnly');

  var url = BASE_URL + '/api/chats?limit=' + chatLimit;

  if (String(hideGroups) === 'true' || String(hideGroups) === '1') url += '&hideGroups=true';
  if (String(unreadOnly) === 'true' || String(unreadOnly) === '1') url += '&unreadOnly=true';

  console.log(">>> Fetching Chat List from URL: " + url);

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  req.onload = function() {
    if (req.readyState === 4 && req.status === 200) {
      var response = JSON.parse(req.responseText);
      var chats = [];

      if (response.length === 0) {
        chats.push({
          'DATA_TYPE': 0,
          'INDEX': 0,
          'CHAT_NAME': "All caught up!",
          'CHAT_PREVIEW': "No unread messages found."
        });
      } else {
        for (var i = 0; i < response.length; i++) {
          chats.push({
            'DATA_TYPE': 0,
            'INDEX': i,
            'CHAT_NAME': response[i].name,
            'CHAT_PREVIEW': response[i].preview,
            'UNREAD_COUNT': response[i].unreadCount || 0
          });
        }
      }
      sendMessages(chats);
    } else {
      console.log("Error fetching chats: " + req.status);
    }
  };
  req.send(null);
}

function fetchChatHistory(chatId) {
  var req = new XMLHttpRequest();
  req.open('GET', BASE_URL + '/api/chats/' + chatId + '/messages?limit=' + currentHistoryLimit, true);
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  req.onload = function() {
    if (req.readyState === 4 && req.status === 200) {
      var response = JSON.parse(req.responseText);
      var history = [];

      // Fallback if the chat is completely empty
      if (response.length === 0) {
        history.push({
          'DATA_TYPE': 1,
          'INDEX': 0,
          'SENDER_NAME': 'System',
          'TIMESTAMP': '',
          'MESSAGE_TEXT': 'No messages yet.',
          'RECEIPT_STATUS': 0
        });
      } else {
        // Queue all the real messages
        for (var i = 0; i < response.length; i++) {

          var msgPayload = {
            'DATA_TYPE': 1,
            'INDEX': i,
            'SENDER_NAME': response[i].sender,
            'TIMESTAMP': response[i].timestamp,
            'MESSAGE_TEXT': response[i].text,
            'RECEIPT_STATUS': response[i].receipt || 0
          };

          // ---> OPTION B: Send the reaction as a separate, dedicated variable! <---
          if (response[i].reaction && response[i].reaction.length > 0) {
            msgPayload[10005] = response[i].reaction; // 10005 is our secret reaction key
          }

          history.push(msgPayload);
        }
      }

      // Send the "End of History" signal so the watch jumps instantly
      history.push({ 'DATA_TYPE': 5 });

      sendMessages(history);
    }
  };
  req.send(null);
}

// ---> THE FIX: Accept the original limit and trigger a history refresh on success! <---
function postReplyToBackend(chatId, messageText, msgIndex, originalLimit) {
  var req = new XMLHttpRequest();
  req.open('POST', BASE_URL + '/api/chats/' + chatId + '/messages', true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  req.onload = function() {
    if (req.readyState === 4 && req.status === 200) {
      console.log("Message successfully delivered to WhatsApp bridge!");

      // ---> FORCE SYNC: Fetch the real history so the watch replaces its optimistic UI! <---
      fetchChatHistory(chatId);
    } else {
      console.log("Error delivering message: " + req.status);
    }
  };

  var payload = {
    text: messageText
  };

  if (msgIndex !== undefined && msgIndex !== -1) {
    payload.msgIndex = msgIndex;
    payload.limit = originalLimit; // Use the old limit so the backend math lines up perfectly!
  }

  req.send(JSON.stringify(payload));
}

function deleteMessageInBackend(chatId, msgIndex) {
  var req = new XMLHttpRequest();
  req.open('POST', BASE_URL + '/api/chats/' + chatId + '/delete', true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  req.onload = function() {
    if (req.readyState === 4 && req.status === 200) {
      console.log("Delete command successfully delivered to WhatsApp bridge!");
      // Force the watch to instantly reload the chat history so the deleted bubble updates!
      fetchChatHistory(chatId);
    } else {
      console.log("Error deleting message: " + req.status);
    }
  };

  req.send(JSON.stringify({
    msgIndex: msgIndex,
    limit: currentHistoryLimit
  }));
}

function postReactionToBackend(chatId, msgIndex, reactionText) {
  var req = new XMLHttpRequest();
  req.open('POST', BASE_URL + '/api/chats/' + chatId + '/react', true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');

  req.onload = function() {
    if (req.readyState === 4 && req.status === 200) {
      console.log("Reaction successfully delivered to WhatsApp bridge!");
    } else {
      console.log("Error delivering reaction: " + req.status);
    }
  };

  req.send(JSON.stringify({
    msgIndex: msgIndex,
    limit: currentHistoryLimit,
    reaction: reactionText
  }));
}
