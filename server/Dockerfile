# Pebble WhatsApp Bridge - Server Container
# Works on both x86_64 and ARM64 hosts (apt pulls the correct Chromium build automatically)

FROM node:20-bookworm-slim

# Install Chromium and the shared libraries Puppeteer needs to run it headlessly
RUN apt-get update && apt-get install -y \
    chromium \
    fonts-liberation \
    libnss3 \
    libatk-bridge2.0-0 \
    libatk1.0-0 \
    libgtk-3-0 \
    libgbm1 \
    libasound2 \
    libxshmfence1 \
    libxrandr2 \
    libxdamage1 \
    libxcomposite1 \
    libx11-xcb1 \
    ca-certificates \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Tell whatsapp-web.js/Puppeteer to use the apt-installed Chromium and skip
# downloading its own (must be set BEFORE npm install, not just at runtime)
ENV PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium
ENV PUPPETEER_SKIP_CHROMIUM_DOWNLOAD=true

# Where the WhatsApp session is persisted - mount this as a volume!
ENV SESSION_PATH=/app/.wwebjs_auth

# Install dependencies first for better layer caching
COPY package*.json ./
COPY patches ./patches
RUN npm install --omit=dev

# Copy the actual application code
COPY server.js ./

EXPOSE 3030

CMD ["node", "server.js"]
