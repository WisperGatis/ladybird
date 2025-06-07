#!/bin/bash

echo "🦎 Mozilla Extension Support Test for Ladybird"
echo "==============================================="

# Create test extension directory
EXTENSION_DIR="/tmp/ladybird-extensions/hello-world-mozilla"

if [ ! -d "$EXTENSION_DIR" ]; then
    echo "📁 Creating Mozilla test extension..."
    mkdir -p "$EXTENSION_DIR"

    # Create Mozilla manifest.json
    cat > "$EXTENSION_DIR/manifest.json" << 'EOF'
{
  "manifest_version": 2,
  "name": "Hello World Mozilla Extension",
  "version": "1.0.0",
  "description": "A test Mozilla WebExtension for Ladybird browser",
  
  "applications": {
    "gecko": {
      "id": "hello-world@ladybird.test",
      "strict_min_version": "57.0"
    }
  },
  
  "permissions": [
    "activeTab",
    "*://*/*"
  ],
  
  "content_scripts": [
    {
      "matches": ["*://*/*"],
      "js": ["content.js"],
      "run_at": "document_idle"
    }
  ],
  
  "background": {
    "scripts": ["background.js"],
    "persistent": false
  },
  
  "browser_action": {
    "default_title": "Hello World Mozilla",
    "default_popup": "popup.html"
  },
  
  "icons": {
    "16": "icon16.png",
    "48": "icon48.png",
    "128": "icon128.png"
  }
}
EOF

    # Create background script
    cat > "$EXTENSION_DIR/background.js" << 'EOF'
console.log('🦎 Mozilla Extension: Background script loaded');

// Test browser.runtime API
if (typeof browser !== 'undefined' && browser.runtime) {
  console.log('Mozilla Extension: browser.runtime API available');
  console.log('Extension ID:', browser.runtime.id);
  
  // Test manifest access
  const manifest = browser.runtime.getManifest();
  console.log('Extension name:', manifest.name);
  console.log('Extension version:', manifest.version);
  
  // Test platform info
  browser.runtime.getPlatformInfo().then(info => {
    console.log('Platform:', info.os, info.arch);
  });
  
  // Test browser info
  browser.runtime.getBrowserInfo().then(info => {
    console.log('Browser:', info.name, info.version);
  });
}

// Extension initialization
console.log('Mozilla Extension: Initialized successfully');
EOF

    # Create content script
    cat > "$EXTENSION_DIR/content.js" << 'EOF'
console.log('🦎 Mozilla Extension: Content script injected into', window.location.href);

// Create a visual indicator
const banner = document.createElement('div');
banner.style.cssText = `
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  background: linear-gradient(135deg, #ff4500, #ff6b35);
  color: white;
  padding: 10px;
  text-align: center;
  font-family: -apple-system, BlinkMacSystemFont, sans-serif;
  font-size: 14px;
  font-weight: bold;
  z-index: 10000;
  box-shadow: 0 2px 10px rgba(0,0,0,0.3);
  border-bottom: 3px solid #d63031;
`;
banner.textContent = '🦎 Hello World Mozilla Extension is active on this page!';
document.body.appendChild(banner);

// Remove banner after 5 seconds
setTimeout(() => {
  if (banner.parentNode) {
    banner.parentNode.removeChild(banner);
  }
}, 5000);

// Test browser.runtime API in content script
if (typeof browser !== 'undefined' && browser.runtime) {
  console.log('Mozilla Extension: browser.runtime API available in content script');
  console.log('Extension ID:', browser.runtime.id);
  
  // Test URL generation
  const iconUrl = browser.runtime.getURL('icon48.png');
  console.log('Extension icon URL:', iconUrl);
}
EOF

    # Create popup HTML
    cat > "$EXTENSION_DIR/popup.html" << 'EOF'
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    body {
      width: 300px;
      padding: 20px;
      font-family: -apple-system, BlinkMacSystemFont, sans-serif;
      background: linear-gradient(135deg, #ff4500, #ff6b35);
      color: white;
      margin: 0;
    }
    .header {
      text-align: center;
      margin-bottom: 15px;
    }
    .info {
      background: rgba(255,255,255,0.1);
      padding: 10px;
      border-radius: 5px;
      margin-bottom: 10px;
    }
    .version {
      font-size: 12px;
      opacity: 0.8;
      text-align: center;
    }
  </style>
</head>
<body>
  <div class="header">
    <h2>🦎 Mozilla Extension</h2>
  </div>
  
  <div class="info">
    <strong>Status:</strong> Active<br>
    <strong>Platform:</strong> Mozilla WebExtensions<br>
    <strong>API:</strong> browser.*
  </div>
  
  <div class="version" id="version">
    Loading version...
  </div>
  
  <script src="popup.js"></script>
</body>
</html>
EOF

    # Create popup script
    cat > "$EXTENSION_DIR/popup.js" << 'EOF'
// Test browser.runtime API in popup
if (typeof browser !== 'undefined' && browser.runtime) {
  const manifest = browser.runtime.getManifest();
  document.getElementById('version').textContent = `Version ${manifest.version}`;
  console.log('Mozilla Extension popup loaded with browser.runtime API');
}
EOF

    echo "✅ Mozilla test extension created in $EXTENSION_DIR"
else
    echo "✅ Mozilla test extension already exists"
fi

echo ""
echo "🚀 Starting Ladybird with Mozilla Extension Support..."
echo ""
echo "📋 Testing Instructions:"
echo "1. Ladybird should open momentarily"
echo "2. Navigate to any website (try: https://example.com)"
echo "3. Look for an orange banner saying '🦎 Hello World Mozilla Extension is active on this page!'"
echo "4. The banner should disappear after 5 seconds"
echo "5. Check browser console (if available) for extension log messages"
echo ""
echo "🎯 Mozilla Extension Features Implemented:"
echo "   ✅ Manifest v2 support with Mozilla extensions"
echo "   ✅ Content script injection"
echo "   ✅ Background script execution"
echo "   ✅ browser.runtime API (Mozilla namespace)"
echo "   ✅ Extension ID from gecko applications field"
echo "   ✅ Platform and browser info APIs"
echo "   ✅ moz-extension:// URL scheme"
echo ""

# Start Ladybird
./Meta/ladybird.py run

echo "�� Testing complete!" 