#!/usr/bin/env bash

# Chrome Extension Test Script for Ladybird Browser
# Uses the modern ladybird.py script

set -e

echo "🧪 Chrome Extension Support Test Script"
echo "======================================="

# Check if build is up to date
echo "📦 Building Ladybird..."
./Meta/ladybird.py build

# Clean up any stale PID files
rm -f ~/Library/Application\ Support/Ladybird.pid

# Create test extension if it doesn't exist
if [ ! -d "/tmp/ladybird-extensions/hello-world-extension" ]; then
    echo "🔧 Creating test extension..."
    mkdir -p /tmp/ladybird-extensions/hello-world-extension
    
    cat > /tmp/ladybird-extensions/hello-world-extension/manifest.json << 'EOF'
{
  "manifest_version": 3,
  "name": "Hello World Extension",
  "version": "1.0.0",
  "description": "A simple test extension to verify Ladybird Chrome extension support",
  "permissions": [
    "activeTab"
  ],
  "content_scripts": [
    {
      "matches": ["*://*/*"],
      "js": ["content.js"],
      "run_at": "document_idle"
    }
  ],
  "background": {
    "service_worker": "background.js"
  },
  "action": {
    "default_title": "Hello World Extension",
    "default_popup": "popup.html"
  }
}
EOF

    cat > /tmp/ladybird-extensions/hello-world-extension/content.js << 'EOF'
// Hello World Extension - Content Script
console.log('Hello World Extension: Content script loaded!');

// Add a visual indicator that the extension is working
const banner = document.createElement('div');
banner.style.cssText = `
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  background: #4CAF50;
  color: white;
  text-align: center;
  padding: 10px;
  font-family: Arial, sans-serif;
  font-size: 14px;
  z-index: 10000;
  box-shadow: 0 2px 5px rgba(0,0,0,0.2);
`;
banner.textContent = '🎉 Hello World Extension is active on this page!';

// Insert banner at the top of the page
document.body.insertBefore(banner, document.body.firstChild);

// Remove banner after 5 seconds
setTimeout(() => {
  if (banner.parentNode) {
    banner.parentNode.removeChild(banner);
  }
}, 5000);

// Test chrome.runtime API
if (typeof chrome !== 'undefined' && chrome.runtime) {
  console.log('Hello World Extension: chrome.runtime API available');
  console.log('Extension ID:', chrome.runtime.id);
}
EOF

    echo "✅ Test extension created in /tmp/ladybird-extensions/hello-world-extension/"
else
    echo "✅ Test extension already exists"
fi

echo ""
echo "🚀 Starting Ladybird with Chrome Extension Support..."
echo ""
echo "📋 Testing Instructions:"
echo "1. Ladybird should open momentarily"
echo "2. Navigate to any website (try: https://example.com)"
echo "3. Look for a green banner saying '🎉 Hello World Extension is active on this page!'"
echo "4. The banner should disappear after 5 seconds"
echo "5. Check browser console (if available) for extension log messages"
echo ""
echo "🎯 Extension Features Implemented:"
echo "   ✅ Manifest v3 support"
echo "   ✅ Content script injection"
echo "   ✅ Background service worker"
echo "   ✅ chrome.runtime API"
echo "   ✅ Development mode extension loading"
echo "   ✅ Chrome-compatible user agent"
echo ""

# Start Ladybird
./Meta/ladybird.py run

echo "�� Testing complete!" 