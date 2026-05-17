var customCSS = "" +
  ":root { " +
    "--bg: #F0F2F5; " +
    "--card: #FFFFFF; " +
    "--text-pri: #111B21; " +
    "--text-sec: #54656F; " +
    "--border: #E9EDEF; " +
    "--input-border: #8696A0; " +
    "--accent: #00A884; " +
    "--highlight: #55FFAA; " +
  "} " +
  "@media (prefers-color-scheme: dark) { " +
    ":root { " +
      "--bg: #111B21; " +
      "--card: #202C33; " +
      "--text-pri: #E9EDEF; " +
      "--text-sec: #8696A0; " +
      "--border: #222E35; " +
      "--input-border: #54656F; " +
    "} " +
  "} " +
  "body { background-color: var(--bg) !important; color: var(--text-pri) !important; font-family: 'Helvetica Neue', Arial, sans-serif !important; padding: 15px !important; margin: 0 !important; } " +
  ".section { background-color: var(--card) !important; border-radius: 16px !important; border: none !important; box-shadow: 0 2px 8px rgba(0,0,0,0.1) !important; margin-bottom: 24px !important; padding: 20px !important; } " +
  ".component-heading { background: transparent !important; border: none !important; margin: 0 !important; padding: 0 !important; box-shadow: none !important; } " +
  "h1, h2, h3, h4, h5, .heading { color: var(--accent) !important; background: transparent !important; font-weight: bold !important; font-size: 13px !important; text-transform: uppercase !important; letter-spacing: 1.2px !important; padding: 0 0 12px 0 !important; border-bottom: 1px solid var(--border) !important; margin-bottom: 20px !important; margin-top: 0 !important; border-radius: 0 !important; box-shadow: none !important; } " +
  ".component-label { color: var(--text-pri) !important; font-weight: 600 !important; font-size: 16px !important; text-shadow: none !important; } " +
  ".component-text, .component-description { color: var(--text-sec) !important; font-size: 14px !important; margin-top: 6px !important; text-shadow: none !important; } " +
  "input[type='text'], input[type='number'], input[type='url'], .component-input input, .component-color__box { border: 1.5px solid var(--input-border) !important; border-radius: 10px !important; background-color: var(--card) !important; color: var(--text-pri) !important; box-shadow: none !important; padding: 12px !important; font-size: 16px !important; transition: all 0.2s ease !important; text-shadow: none !important; width: 100% !important; box-sizing: border-box !important; } " +
  "input[type='text']:focus, input[type='number']:focus, input[type='url']:focus, .component-input input:focus { border-color: var(--accent) !important; background-color: var(--card) !important; box-shadow: 0 0 0 3px rgba(0,168,132,0.15) !important; outline: none !important; } " +
  ".component-slider__box { flex: 0 0 75px !important; width: 75px !important; min-width: 75px !important; margin-left: 12px !important; } " +
  "input[type='number'] { height: 42px !important; padding: 0 14px 0 0 !important; text-align: center !important; font-weight: bold !important; font-size: 18px !important; -moz-appearance: textfield !important; -webkit-appearance: none !important; appearance: none !important; } " +
  "input[type='number']::-webkit-inner-spin-button, input[type='number']::-webkit-outer-spin-button { -webkit-appearance: none !important; display: none !important; margin: 0 !important; } " +
  ".component-button .button, button { background-color: var(--accent) !important; color: #FFFFFF !important; border-radius: 24px !important; border: none !important; font-size: 16px !important; font-weight: bold !important; padding: 16px !important; box-shadow: 0 4px 12px rgba(0,168,132,0.3) !important; text-transform: uppercase !important; letter-spacing: 1px !important; width: 100% !important; margin-top: 10px !important; cursor: pointer !important; text-shadow: none !important; } " +
  ".component-button .button:active, button:active { background-color: #008f6f !important; } " +
  ".component-toggle__box { background-color: var(--border) !important; border-color: var(--border) !important; } " +
  ".component-toggle input:checked + .component-toggle__box { background-color: var(--highlight) !important; border-color: var(--highlight) !important; } " +
  ".component-toggle input:checked + .component-toggle__box .component-toggle__knob { background-color: var(--accent) !important; border-color: var(--accent) !important; } " +
  ".component-slider__track { background-color: var(--border) !important; } " +
  ".component-slider__track--active { background-color: var(--highlight) !important; } " +
  ".component-slider__knob { background-color: var(--accent) !important; border-color: var(--accent) !important; box-shadow: 0 2px 4px rgba(0,0,0,0.2) !important; } " +
  ".component-color { border: none !important; padding: 0 !important; margin-bottom: 12px !important; background: transparent !important; } " +
  ".component-color__box { padding: 0 !important; width: 36px !important; height: 36px !important; border-radius: 18px !important; min-width: 36px !important; }";

module.exports = [
  {
    "type": "heading",
    "defaultValue": "WhatsApp for Pebble <style>" + customCSS + "</style>"
  },
  {
    "type": "text",
    "defaultValue": "Configure your connection, layout, and custom Pebble quick replies."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Connection Setup"
      },
      {
        "type": "input",
        "messageKey": "ServerURL",
        "defaultValue": "",
        "label": "Server URL",
        "description": "Your Raspberry Pi's IP or Tailscale address (e.g., http://192.168.0.30:3000)"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Memory & Performance"
      },
      {
        "type": "slider",
        "messageKey": "ChatLimit",
        "defaultValue": 8,
        "label": "Max Chats to Load",
        "min": 1,
        "max": 15,
        "step": 1
      },
      {
        "type": "slider",
        "messageKey": "MessageLimit",
        "defaultValue": 10,
        "label": "Messages per Chat",
        "min": 1,
        "max": 20,
        "step": 1
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Filters"
      },
      {
        "type": "toggle",
        "messageKey": "HideGroupChats",
        "label": "Hide Group Chats",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "UnreadOnly",
        "label": "Show Unread Only",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Theme Colors"
      },
      {
        "type": "color",
        "messageKey": "THEME_ME",
        "defaultValue": "00AA55",
        "label": "My Messages Color"
      },
      {
        "type": "color",
        "messageKey": "THEME_OTHER",
        "defaultValue": "AAAAAA",
        "label": "Other Messages Color"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Custom Quick Replies"
      },
      {
        "type": "input",
        "messageKey": "Reply1",
        "defaultValue": "Yes",
        "label": "Reply Slot 1",
        "attributes": {
          "maxlength": 30
        }
      },
      {
        "type": "input",
        "messageKey": "Reply2",
        "defaultValue": "No",
        "label": "Reply Slot 2",
        "attributes": {
          "maxlength": 30
        }
      },
      {
        "type": "input",
        "messageKey": "Reply3",
        "defaultValue": "I'll call you later",
        "label": "Reply Slot 3",
        "attributes": {
          "maxlength": 30
        }
      },
      {
        "type": "input",
        "messageKey": "Reply4",
        "defaultValue": "OK",
        "label": "Reply Slot 4",
        "attributes": {
          "maxlength": 30
        }
      },
      {
        "type": "input",
        "messageKey": "Reply5",
        "defaultValue": "On my way!",
        "label": "Reply Slot 5",
        "attributes": {
          "maxlength": 30
        }
      },
      {
        "type": "input",
        "messageKey": "Reply6",
        "defaultValue": "Can't talk right now.",
        "label": "Reply Slot 6",
        "attributes": {
          "maxlength": 30
        }
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];