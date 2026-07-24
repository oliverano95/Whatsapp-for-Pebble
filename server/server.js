const express = require('express');
const http = require('http');
const { Client, LocalAuth } = require('whatsapp-web.js');
const qrcode = require('qrcode-terminal');
const app = express();

app.use(express.json());

// ==============================================================================
// 0. SECURITY: API KEY MIDDLEWARE
// ==============================================================================
const API_SECRET = process.env.API_KEY;

if (!API_SECRET) {
    console.error('FATAL: API_KEY environment variable is not set. Refusing to start.');
    process.exit(1);
}

app.use((req, res, next) => {
    // We leave the /clay settings page and /health check public
    if (req.path === '/clay' || req.path === '/health') return next();

    // Enforce the password on all other routes
    const clientKey = req.headers['x-api-key'];
    if (clientKey === API_SECRET) {
        next(); // Password matches, proceed!
    } else {
        console.warn(`Blocked unauthorized access attempt to ${req.path}`);
        res.status(401).send("Unauthorized: Invalid API Key");
    }
});

// ==============================================================================
// 1. STATE & MAPPING
// ==============================================================================
let isClientReady = false;
let chatMap = []; // Maps integer indices from the Pebble back to real WhatsApp IDs

const REACTION_MAP = {
    "0": "👍", "1": "❤️", "2": "😂",
    "3": "😮", "4": "😢", "5": "🙏"
};

// ==============================================================================
// 2. WHATSAPP CLIENT SETUP
// ==============================================================================
console.log("Initializing WhatsApp Client... This takes a few seconds.");

// Allows overriding the Chromium binary path per-environment (Pi, Docker, etc.)
// Falls back to letting Puppeteer use its own bundled Chromium if unset.
const chromiumPath = process.env.PUPPETEER_EXECUTABLE_PATH || undefined;

const client = new Client({
    authStrategy: new LocalAuth({ dataPath: process.env.SESSION_PATH || '.wwebjs_auth' }),
    authTimeoutMs: 120000,
    puppeteer: {
        executablePath: chromiumPath,
        timeout: 120000,
        protocolTimeout: 240000,
        args: [
            '--no-sandbox', '--disable-setuid-sandbox', '--disable-dev-shm-usage',
            '--disable-accelerated-2d-canvas', '--no-first-run', '--no-zygote',
            '--disable-gpu', '--mute-audio', '--disable-extensions',
            '--disable-background-networking', '--disable-default-apps', '--disable-sync'
        ]
    }
});

client.on('qr', (qr) => {
    console.log('\n>>> SCAN THIS QR CODE WITH YOUR WHATSAPP APP: <<<');
    qrcode.generate(qr, { small: true });
});

client.on('ready', () => {
    isClientReady = true;
    console.log('\n>>> WHATSAPP IS READY! Your Pebble is connected to the real world. <<<\n');
});

client.on('disconnected', (reason) => {
    isClientReady = false;
    console.log('WhatsApp Client disconnected:', reason);
});

client.on('auth_failure', msg => {
    console.error('Authentication failure!', msg);
});

client.initialize();

// ==============================================================================
// 3. HELPER FUNCTIONS
// ==============================================================================

function getMediaLabel(msg) {
    switch (msg.type) {
        case 'image': return '[Image]';
        case 'video': return '[Video]';
        case 'audio': return '[Audio]';
        case 'ptt': return '[Voice Note]';
        case 'location': return '[Location]';
        case 'document': return '[Document]';
        case 'sticker': return '[Sticker]';
        case 'vcard':
        case 'multi_vcard': return '[Contact]';
        case 'revoked': return '[Deleted]';
        default: return '[Attachment]';
    }
}

function getPebbleReaction(emoji) {
    if (emoji.includes('👍')) return 'Like';
    if (emoji.includes('❤️') || emoji.includes('❤')) return 'Love';
    if (emoji.includes('😂')) return 'Haha';
    if (emoji.includes('😮')) return 'Wow';
    if (emoji.includes('😢')) return 'Sad';
    if (emoji.includes('🙏')) return 'Pray';
    return 'Star';
}

// Consolidates the logic for finding a specific message in a chat to React/Reply/Delete
async function getTargetMessage(integerId, msgIndex, limit) {
    const realChatId = chatMap[integerId];
    if (!realChatId) throw new Error("CHAT_NOT_FOUND");

    const chat = await client.getChatById(realChatId);
    if (msgIndex === undefined || msgIndex === null) return { chat, targetMsg: null };

    const messages = await chat.fetchMessages({ limit: limit || 10 });
    if (messages && messages[msgIndex]) {
        return { chat, targetMsg: messages[msgIndex] };
    } else {
        throw new Error("MESSAGE_OUT_OF_BOUNDS");
    }
}

// ==============================================================================
// 4. API ENDPOINTS
// ==============================================================================

// --- GET: Live Chat List ---
app.get('/api/chats', async (req, res) => {
    try {
        console.log("\n--- Incoming Pebble Request: /api/chats ---");
        const chats = await client.getChats();

        const limit = parseInt(req.query.limit) || 8;
        const hideGroups = req.query.hideGroups === 'true';
        const unreadOnly = req.query.unreadOnly === 'true';

        // Filter and slice chats
        const recentChats = chats.filter(chat => {
            if (hideGroups && chat.isGroup) return false;
            if (unreadOnly && chat.unreadCount === 0) return false;
            return true;
        }).slice(0, limit);

        chatMap = []; // Reset the map

        const formattedChats = await Promise.all(recentChats.map(async (chat, index) => {
            chatMap[index] = chat.id._serialized;

            let previewText = "No messages";
            if (chat.lastMessage) {
                let senderPrefix = "";
                if (chat.isGroup && !chat.lastMessage.fromMe) {
                    try {
                        const contact = await chat.lastMessage.getContact();
                        senderPrefix = `${contact.shortName || contact.pushname || contact.number}: `;
                    } catch (e) {
                        /* Ignore contact fetch errors for previews */
                    }
                }

                if (chat.lastMessage.hasMedia || chat.lastMessage.type !== 'chat') {
                    const label = getMediaLabel(chat.lastMessage);
                    previewText = `${senderPrefix}${label} ${chat.lastMessage.body || ''}`.trim();
                } else {
                    previewText = `${senderPrefix}${chat.lastMessage.body || "No messages"}`;
                }

                if (previewText.length > 30) previewText = previewText.substring(0, 27) + "...";
            }

            return {
                id: index,
                name: chat.name,
                preview: previewText,
                unreadCount: chat.unreadCount
            };
        }));

        res.json(formattedChats);
    } catch (err) {
        console.error("Error fetching chats:", err);
        res.status(500).send("Error fetching chats");
    }
});

// --- GET: Message History ---
app.get('/api/chats/:id/messages', async (req, res) => {
    try {
        const realChatId = chatMap[parseInt(req.params.id)];
        if (!realChatId) return res.status(404).send("Chat not found");

        console.log(`\nPebble requested history for real chat: ${realChatId}`);
        const chat = await client.getChatById(realChatId);

        if (chat.unreadCount > 0) {
            chat.sendSeen().catch(e => console.error("Could not send seen receipt:", e));
        }

        const limit = parseInt(req.query.limit) || 10;
        const messages = await chat.fetchMessages({ limit: limit });

        const formattedMessages = await Promise.all(messages.map(async msg => {
            const date = new Date(msg.timestamp * 1000);
            const timeStr = `${date.getHours().toString().padStart(2, '0')}:${date.getMinutes().toString().padStart(2, '0')}`;

            let senderName = msg.fromMe ? "Me" : chat.name;
            if (chat.isGroup && !msg.fromMe) {
                try {
                    const contact = await msg.getContact();
                    senderName = contact.shortName || contact.pushname || contact.number;
                } catch (e) { }
            }

            let text = msg.body;
            if (msg.hasMedia || msg.type !== 'chat') {
                const label = getMediaLabel(msg);
                text = msg.body ? `${label} ${msg.body}` : label;
            }

            let reactionStr = "";
            if (msg.hasReaction) {
                try {
                    const reactions = await msg.getReactions();
                    if (reactions && reactions.length > 0) {
                        const safeReactions = [...new Set(reactions.map(r => getPebbleReaction(r.aggregateEmoji)))];
                        reactionStr = safeReactions.join('');
                    }
                } catch (e) {
                    console.error("Failed to fetch reactions", e);
                }
            }

            return {
                sender: senderName,
                timestamp: timeStr,
                text: text || "[Empty]",
                reaction: reactionStr,
                receipt: msg.fromMe ? msg.ack : 0
            };
        }));

        res.json(formattedMessages);
    } catch (err) {
        console.error("Error fetching history:", err);
        res.status(500).send("Error fetching history");
    }
});

// --- POST: Send Message / Reply ---
app.post('/api/chats/:id/messages', async (req, res) => {
    try {
        const { text, msgIndex, limit } = req.body;
        const { chat, targetMsg } = await getTargetMessage(parseInt(req.params.id), msgIndex, limit);

        if (targetMsg) {
            console.log(`\nSending QUOTED reply to ${chat.id._serialized} on index ${msgIndex}: "${text}"`);
            await targetMsg.reply(text);
        } else {
            console.log(`\nSending normal message to ${chat.id._serialized}: "${text}"`);
            await client.sendMessage(chat.id._serialized, text);
        }

        console.log(">>> SUCCESS! Message delivered! <<<");
        res.sendStatus(200);
    } catch (err) {
        if (err.message === "CHAT_NOT_FOUND") return res.status(404).send("Chat not found");
        if (err.message === "MESSAGE_OUT_OF_BOUNDS") {
            // Fallback: If quote fails, just send a normal message
            console.error(`Quote index out of bounds! Falling back to normal message.`);
            const realChatId = chatMap[parseInt(req.params.id)];
            await client.sendMessage(realChatId, req.body.text);
            return res.sendStatus(200);
        }
        console.error("Error sending message:", err);
        res.status(500).send("Error sending message");
    }
});

// --- POST: React to Message ---
app.post('/api/chats/:id/react', async (req, res) => {
    try {
        const { msgIndex, limit, reaction } = req.body;
        const { targetMsg } = await getTargetMessage(parseInt(req.params.id), msgIndex, limit);

        const actualEmoji = REACTION_MAP[reaction] || reaction;
        console.log(`\nApplying reaction '${actualEmoji}' to message index ${msgIndex}`);

        await targetMsg.react(actualEmoji);
        console.log(">>> SUCCESS! Reaction delivered! <<<");
        res.sendStatus(200);

    } catch (err) {
        if (err.message === "CHAT_NOT_FOUND") return res.status(404).send("Chat not found");
        if (err.message === "MESSAGE_OUT_OF_BOUNDS") return res.status(404).send("Message not found");
        console.error("Error sending reaction:", err);
        res.status(500).send("Error sending reaction");
    }
});

// --- POST: Delete Message ---
app.post('/api/chats/:id/delete', async (req, res) => {
    try {
        const { msgIndex, limit } = req.body;
        const { targetMsg } = await getTargetMessage(parseInt(req.params.id), msgIndex, limit);

        if (targetMsg.fromMe) {
            await targetMsg.delete(true);
            console.log(">>> SUCCESS! Message deleted for everyone! <<<");
            res.sendStatus(200);
        } else {
            console.error("Forbidden: Cannot delete a message sent by someone else!");
            res.status(403).send("Forbidden");
        }
    } catch (err) {
        if (err.message === "CHAT_NOT_FOUND") return res.status(404).send("Chat not found");
        if (err.message === "MESSAGE_OUT_OF_BOUNDS") return res.status(404).send("Message not found");
        console.error("Error deleting message:", err);
        res.status(500).send("Error deleting message");
    }
});


// ==============================================================================
// 5. SERVER BOOT & UTILITIES
// ==============================================================================

// Host the Clay Settings Page (Steam Deck / KDE Fix)
app.get('/clay', (req, res) => {
    res.send(`
    <!DOCTYPE html>
    <html>
    <head><title>Pebble Config</title></head>
    <body>
    <script>
    var urlParams = new URLSearchParams(window.location.search);
    var returnTo = urlParams.get('return_to') || 'pebblejs://close#';
    var payload = window.location.hash.substring(1);
    var decodedHtml = decodeURIComponent(payload);
    var finalHtml = decodedHtml.replace(/\\$\\$RETURN_TO\\$\\$/g, returnTo);
    document.write(finalHtml);
    document.close();
    </script>
    </body>
    </html>
    `);
});

// Uptime Kuma Health Check
app.get('/health', (req, res) => {
    if (isClientReady) {
        res.status(200).send("OK: WhatsApp Bridge is fully operational.");
    } else {
        res.status(503).send("ERROR: WhatsApp Client is offline or initializing.");
    }
});

// --- Server Boot ---
const PORT = process.env.PORT || 3001;
const httpServer = http.createServer(app);
httpServer.listen(PORT, '0.0.0.0', () => {
    console.log(`HTTP Server running on port ${PORT} (Ready for Cloudflare Tunnel)`);
});
