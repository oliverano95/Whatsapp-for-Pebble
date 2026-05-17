// --- CLAY INTEGRATION ---
var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Initialize Clay, but turn OFF auto-handling so we don't send the URL to the watch!
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var BASE_URL = 'http://127.0.0.1:3000'; // Default fallback
var currentHistoryLimit = 10; // NEW: Track the pagination limit for reactions!

Pebble.addEventListener('showConfiguration', function(e) {
  Pebble.openURL(clay.generateUrl());
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
    currentHistoryLimit = parseInt(localStorage.getItem('MessageLimit')) || 10; // Reset limit on new chat
    fetchChatHistory(chatId);
    
  } else if (type === 2) {
    console.log("User replied to chat " + dict['CHAT_ID'] + " with message: " + dict['MESSAGE_TEXT']);
    postReplyToBackend(dict['CHAT_ID'], dict['MESSAGE_TEXT']);
    
  } else if (type === 3) { // PAGINATION
    currentHistoryLimit += 10; 
    console.log("Pagination triggered! Fetching " + currentHistoryLimit + " messages for chat " + dict['CHAT_ID']);
    fetchChatHistory(dict['CHAT_ID']);
    
  } else if (type === 4) { // REACTION
    console.log("User reacted to chat " + dict['CHAT_ID'] + " msg index " + dict['INDEX'] + " with: " + dict['MESSAGE_TEXT']);
    postReactionToBackend(dict['CHAT_ID'], dict['INDEX'], dict['MESSAGE_TEXT']);
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
            'CHAT_PREVIEW': response[i].preview
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
      
      for (var i = 0; i < response.length; i++) {
        history.push({
          'DATA_TYPE': 1, 
          'INDEX': i, 
          'SENDER_NAME': response[i].sender, 
          'TIMESTAMP': response[i].timestamp, 
          'MESSAGE_TEXT': response[i].text,
          'RECEIPT_STATUS': response[i].receipt || 0 
        });
      }
      sendMessages(history);
    }
  };
  req.send(null);
}

function postReplyToBackend(chatId, messageText) {
  var req = new XMLHttpRequest();
  req.open('POST', BASE_URL + '/api/chats/' + chatId + '/messages', true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('Bypass-Tunnel-Reminder', 'true');
  
  req.onload = function() {
    console.log("Message successfully delivered to WhatsApp bridge!");
  };
  req.send(JSON.stringify({ text: messageText }));
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
  
  // Pass currentHistoryLimit so the backend perfectly matches the exact message array!
  req.send(JSON.stringify({ 
    msgIndex: msgIndex, 
    limit: currentHistoryLimit, 
    reaction: reactionText 
  }));
}