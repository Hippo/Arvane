(function () {
  const statusLine = document.getElementById('status-line');
  const canvas = document.getElementById('arvane-canvas');
  const shellRoot = document.querySelector('.web-client');
  const connectionPanel = document.querySelector('.session-panel');
  const connectButton = document.getElementById('connect-button');
  const disconnectButton = document.getElementById('disconnect-button');
  const serverList = document.getElementById('server-list');
  const serverForm = document.getElementById('server-form');
  const serverName = document.getElementById('server-name');
  const serverUrl = document.getElementById('server-url');
  const newServerButton = document.getElementById('new-server-button');
  const editServerButton = document.getElementById('edit-server-button');
  const removeServerButton = document.getElementById('remove-server-button');
  const serverDialog = document.getElementById('server-dialog');
  const serverDialogHeading = document.getElementById('server-dialog-heading');
  const serverDialogMode = document.getElementById('server-dialog-mode');
  const profileButton = document.getElementById('profile-button');
  const profileDialog = document.getElementById('profile-dialog');
  const profileForm = document.getElementById('profile-form');
  const playerName = document.getElementById('player-name');
  const profileDisplayName = document.getElementById('profile-display-name');
  const profileAvatar = document.getElementById('profile-avatar');
  const saveProfileButton = document.getElementById('save-profile-button');
  const skinSummary = document.getElementById('skin-summary');
  const skinDetail = document.getElementById('skin-detail');
  const skinModel = document.getElementById('skin-model');
  const skinFile = document.getElementById('skin-file');
  const pickSkinButton = document.getElementById('pick-skin-button');
  const clientConfigPath = '/arvane_home/config/client.yaml';
  const serverListPath = '/arvane_home/config/server_list.json';
  const importDirectoryPath = '/arvane_home/imports';
  const sessionLogPath = '/arvane_home/logs/web-session.log';
  const defaultServerUrl = serverUrl ? serverUrl.value : 'ws://localhost:42070/arvane';
  const defaultServers = [{ name: 'Local Game', address: defaultServerUrl }];
  const pendingLogLines = [];
  const runtimeCacheKey = String(Date.now());
  let storageMetadataReady = false;
  let runtimeReady = false;
  let shellState = 'boot';
  let panelParent = connectionPanel ? connectionPanel.parentNode : null;
  let panelMarker = null;
  let servers = defaultServers.slice();
  let selectedServerIndex = 0;
  let editingServerIndex = -1;
  let serverFormSnapshot = null;
  let serverDialogCommitted = false;
  let statusClearTimer = 0;
  let lastServerClickIndex = -1;
  let lastServerClickTime = 0;
  let profileDirty = false;
  let currentSkinHash = '';
  const serverDoubleClickWindowMs = 420;

  if (connectionPanel && panelParent) {
    panelMarker = document.createComment('arvane-session-panel');
    panelParent.insertBefore(panelMarker, connectionPanel);
  }

  function timestamp() {
    return new Date().toISOString();
  }

  function yamlString(value) {
    return JSON.stringify(String(value));
  }

  function parseYamlString(value) {
    const trimmed = String(value || '').trim();
    if (!trimmed) {
      return '';
    }
    if (trimmed.startsWith('"')) {
      try {
        return JSON.parse(trimmed);
      } catch (error) {
        return '';
      }
    }
    return trimmed.replace(/^['"]|['"]$/g, '');
  }

  function fileExists(path) {
    return typeof FS !== 'undefined' &&
      typeof FS.analyzePath === 'function' &&
      FS.analyzePath(path).exists;
  }

  function ensureDirectory(path) {
    if (typeof FS !== 'undefined' &&
        typeof FS.analyzePath === 'function' &&
        FS.analyzePath(path).exists) {
      return;
    }
    try {
      FS.mkdir(path);
    } catch (error) {
      if (typeof FS === 'undefined' ||
          typeof FS.analyzePath !== 'function' ||
          !FS.analyzePath(path).exists) {
        throw error;
      }
    }
  }

  function flushPendingLogs() {
    if (!storageMetadataReady || typeof FS === 'undefined' || pendingLogLines.length === 0) {
      return;
    }
    try {
      const prefix = fileExists(sessionLogPath)
        ? FS.readFile(sessionLogPath, { encoding: 'utf8' })
        : '';
      const lines = pendingLogLines.join('');
      FS.writeFile(sessionLogPath, prefix + lines);
      pendingLogLines.splice(0);
    } catch (error) {
      console.warn('[arv_web] log write failed', error);
    }
  }

  function appendWebLog(text) {
    pendingLogLines.push(`${timestamp()} ${String(text).replace(/\s+/g, ' ')}\n`);
    flushPendingLogs();
  }

  function friendlyStatus(text) {
    const raw = String(text || '').trim();
    const lower = raw.toLowerCase();
    if (!raw) {
      return '';
    }
    if ([
      'browser storage ready',
      'ready',
      'ready (storage mounted)',
      'storage synced',
      'focused',
      'focus lost',
      'visible',
      'paused while hidden',
      'pointer locked',
      'pointer released',
      'webgl2 ready',
      'web block textures and models ready',
      'web entity models, textures, and animations ready',
      'remote chunk mesh uploaded'
    ].includes(lower)) {
      return '';
    }
    if ([
      'loading runtime',
      'mounting browser storage',
      'loading browser storage',
      'starting arvane web client'
    ].includes(lower)) {
      return 'Starting Arvane...';
    }
    if (lower === 'ready (storage unavailable)' || lower.includes('idbfs')) {
      return "Couldn't prepare your browser save data. Refresh and try again.";
    }
    if (lower === 'runtime is not ready yet' ||
        lower === 'browser storage is not ready') {
      return 'Arvane is still starting. Try again in a moment.';
    }
    if (lower === 'enter a websocket url' || lower === 'missing websocket url') {
      return 'Enter a server address.';
    }
    if (lower === 'server saved') {
      return 'Server saved.';
    }
    if (lower === 'server removed') {
      return 'Server removed.';
    }
    if (lower === 'failed to load runtime script' ||
        lower.startsWith('runtime aborted') ||
        lower === 'browser runtime error') {
      return "Couldn't start Arvane. Refresh and try again.";
    }
    if (lower === 'connecting websocket' ||
        lower === 'transport connected' ||
        lower === 'protocol handshake' ||
        lower === 'registry sync') {
      return 'Joining server...';
    }
    if (lower === 'loading world') {
      return 'Loading world...';
    }
    if (lower === 'in game') {
      return 'Entering world...';
    }
    if (lower === 'disconnecting') {
      return 'Leaving server...';
    }
    if (lower === 'disconnected') {
      return 'Disconnected.';
    }
    if (lower.startsWith('disconnected:')) {
      return 'Disconnected. You can choose another server.';
    }
    if (lower.startsWith('websocket connect failed')) {
      return "Couldn't join that server. Check the address and try again.";
    }
    if (lower.startsWith('server registry verified')) {
      return 'Preparing world...';
    }
    if (lower.endsWith('ready')) {
      return '';
    }
    if (lower.startsWith('storage') && lower.includes('failed')) {
      return "Couldn't save changes. Try again.";
    }
    if (lower.startsWith('server list save failed')) {
      return "Couldn't save that server. Try again.";
    }
    if (lower.startsWith('profile saved')) {
      return 'Profile saved.';
    }
    if (lower.startsWith('profile load failed')) {
      return "Couldn't load your profile.";
    }
    if (lower.startsWith('profile save failed')) {
      return "Couldn't save your profile.";
    }
    if (lower.startsWith('skin saved')) {
      return 'Skin saved.';
    }
    if (lower.startsWith('skin model saved')) {
      return 'Skin model saved.';
    }
    if (lower.startsWith('skin import failed')) {
      return "Couldn't import that skin. Choose a PNG file and try again.";
    }
    if (lower.includes('websocket')) {
      return lower.includes('failed')
        ? "Couldn't join that server. Check the address and try again."
        : 'Joining server...';
    }
    if (lower.includes('storage')) {
      return lower.includes('failed') || lower.includes('unavailable')
        ? "Couldn't save changes. Try again."
        : 'Ready';
    }
    return raw;
  }

  function statusTone(raw, displayText) {
    const lower = `${raw} ${displayText}`.toLowerCase();
    return lower.includes('failed') ||
      lower.includes("couldn't") ||
      lower.includes('unavailable') ||
      lower.includes('aborted') ||
      lower.includes('error') ? 'error' : 'neutral';
  }

  function statusIsEphemeral(raw) {
    const lower = String(raw || '').toLowerCase();
    return lower === 'server saved' ||
      lower === 'server removed' ||
      lower.startsWith('profile saved') ||
      lower.startsWith('skin saved') ||
      lower.startsWith('skin model saved');
  }

  function setStatus(text) {
    const raw = String(text || '').trim();
    if (!raw) {
      return;
    }
    const displayText = friendlyStatus(raw);
    if (statusLine) {
      if (statusClearTimer) {
        window.clearTimeout(statusClearTimer);
        statusClearTimer = 0;
      }
      if (displayText) {
        statusLine.textContent = displayText;
        statusLine.hidden = false;
        statusLine.dataset.tone = statusTone(raw, displayText);
        if (statusIsEphemeral(raw)) {
          statusClearTimer = window.setTimeout(function () {
            statusLine.textContent = '';
            statusLine.hidden = true;
            statusLine.dataset.tone = 'neutral';
            statusClearTimer = 0;
          }, 2800);
        }
      } else {
        statusLine.textContent = '';
        statusLine.hidden = true;
        statusLine.dataset.tone = 'neutral';
      }
    }
    appendWebLog(raw);
    console.log('[arv_web]', raw);
  }

  window.arvWebSetStatus = setStatus;

  function isLocalServer(address) {
    try {
      const parsed = new URL(address);
      return parsed.hostname === 'localhost' || parsed.hostname === '127.0.0.1';
    } catch (error) {
      return String(address || '').includes('localhost') ||
        String(address || '').includes('127.0.0.1');
    }
  }

  function deriveServerName(address) {
    try {
      const parsed = new URL(address);
      if (parsed.hostname === 'localhost' || parsed.hostname === '127.0.0.1') {
        return 'Local Game';
      }
      return parsed.hostname || 'Server';
    } catch (error) {
      const compact = String(address || '').replace(/^wss?:\/\//, '').split('/')[0];
      return compact || 'Server';
    }
  }

  function displayServerName(server) {
    if (!server) {
      return 'No server selected';
    }
    const name = String(server.name || '').trim();
    if ((!name || name === 'Local') && isLocalServer(server.address)) {
      return 'Local Game';
    }
    return name || deriveServerName(server.address);
  }

  function friendlyServerDetail(address) {
    if (!address) {
      return 'Add a server to play.';
    }
    if (isLocalServer(address)) {
      return 'Local server';
    }
    try {
      const parsed = new URL(address);
      const path = parsed.pathname && parsed.pathname !== '/' ? parsed.pathname : '';
      return `${parsed.host || parsed.hostname}${path}`;
    } catch (error) {
      return String(address).replace(/^wss?:\/\//, '') || 'Server address saved';
    }
  }

  function normalizeServer(entry) {
    const name = String(entry && entry.name ? entry.name : '').trim();
    const address = String(entry && entry.address ? entry.address : '').trim();
    if (!address) {
      return null;
    }
    return {
      name: name || deriveServerName(address),
      address
    };
  }

  function activeServer() {
    if (selectedServerIndex >= 0 && selectedServerIndex < servers.length) {
      return servers[selectedServerIndex];
    }
    const address = serverUrl ? serverUrl.value.trim() : defaultServerUrl;
    if (!address) {
      return null;
    }
    return {
      name: deriveServerName(address),
      address
    };
  }

  function activeServerAddress() {
    const selected = activeServer();
    return selected ? selected.address : '';
  }

  function updateSelectedServerLine() {
    updateControlState();
  }

  function setSelectedServer(index) {
    selectedServerIndex = Number.isInteger(index) &&
      index >= 0 &&
      index < servers.length ? index : -1;
    if (selectedServerIndex >= 0) {
      const selected = servers[selectedServerIndex];
      if (serverName) {
        serverName.value = selected.name;
      }
      if (serverUrl) {
        serverUrl.value = selected.address;
      }
    }
    renderServerList();
    updateSelectedServerLine();
  }

  function renderServerList() {
    if (!serverList) {
      return;
    }
    serverList.textContent = '';
    if (servers.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'empty-server-list';
      empty.textContent = 'No saved servers yet.';
      serverList.appendChild(empty);
      serverList.removeAttribute('aria-activedescendant');
      if (editServerButton) {
        editServerButton.disabled = true;
      }
      return;
    }

    servers.forEach((server, index) => {
      const entry = document.createElement('button');
      const entryId = `server-option-${index}`;
      entry.id = entryId;
      entry.type = 'button';
      entry.className = 'server-entry';
      entry.setAttribute('role', 'option');
      entry.setAttribute('aria-selected', index === selectedServerIndex ? 'true' : 'false');
      entry.setAttribute(
        'aria-label',
        `${displayServerName(server)}, ${friendlyServerDetail(server.address)}`);

      const copy = document.createElement('span');
      copy.className = 'server-copy';
      const title = document.createElement('strong');
      title.textContent = displayServerName(server);
      const address = document.createElement('span');
      address.textContent = friendlyServerDetail(server.address);
      copy.append(title, address);

      entry.append(copy);
      entry.addEventListener('click', function () {
        const now = window.performance && typeof window.performance.now === 'function'
          ? window.performance.now()
          : Date.now();
        const repeatedClick = lastServerClickIndex === index &&
          now - lastServerClickTime <= serverDoubleClickWindowMs;
        setSelectedServer(index);
        lastServerClickIndex = index;
        lastServerClickTime = now;
        if (repeatedClick) {
          lastServerClickIndex = -1;
          lastServerClickTime = 0;
          joinActiveServer();
        }
      });
      entry.addEventListener('keydown', function (event) {
        if (event.key === 'Enter') {
          event.preventDefault();
          event.stopPropagation();
          setSelectedServer(index);
          joinActiveServer();
        }
      });
      serverList.appendChild(entry);
    });

    if (selectedServerIndex >= 0 && selectedServerIndex < servers.length) {
      serverList.setAttribute('aria-activedescendant', `server-option-${selectedServerIndex}`);
    } else {
      serverList.removeAttribute('aria-activedescendant');
    }
    if (editServerButton) {
      editServerButton.disabled = selectedServerIndex < 0;
    }
  }

  function readJsonFile(path) {
    if (!fileExists(path)) {
      return null;
    }
    return JSON.parse(FS.readFile(path, { encoding: 'utf8' }));
  }

  function writeJsonFile(path, value) {
    FS.writeFile(path, `${JSON.stringify(value, null, 2)}\n`);
  }

  function loadServerList() {
    let loadedServers = fileExists(serverListPath) ? [] : defaultServers.slice();
    try {
      const root = readJsonFile(serverListPath);
      if (root !== null) {
        const source = Array.isArray(root) ? root : root.servers;
        if (Array.isArray(source)) {
          loadedServers = source.map(normalizeServer).filter(Boolean);
        }
      }
    } catch (error) {
      appendWebLog(`server list load failed: ${error}`);
      loadedServers = defaultServers.slice();
    }
    servers = loadedServers;
    const configuredUrl = serverUrl ? serverUrl.value.trim() : '';
    const configuredIndex = servers.findIndex((server) => server.address === configuredUrl);
    if (configuredIndex >= 0) {
      setSelectedServer(configuredIndex);
    } else {
      selectedServerIndex = configuredUrl ? -1 : (servers.length > 0 ? 0 : -1);
      if (selectedServerIndex >= 0 && serverUrl) {
        serverUrl.value = servers[selectedServerIndex].address;
      }
      renderServerList();
      updateSelectedServerLine();
    }
  }

  function saveServerList() {
    if (!storageMetadataReady || typeof FS === 'undefined') {
      return;
    }
    try {
      writeJsonFile(serverListPath, {
        version: 1,
        servers
      });
      appendWebLog('server list saved');
    } catch (error) {
      setStatus(`Server list save failed: ${error}`);
    }
  }

  function openDialog(dialog, focusTarget) {
    if (!dialog || typeof dialog.showModal !== 'function') {
      return;
    }
    if (!dialog.open) {
      dialog.showModal();
    }
    window.setTimeout(function () {
      if (focusTarget && typeof focusTarget.focus === 'function') {
        focusTarget.focus();
      }
    }, 0);
  }

  function closeDialog(dialog) {
    if (dialog && dialog.open) {
      dialog.close();
    }
  }

  function closeAllDialogs() {
    closeDialog(profileDialog);
    closeDialog(serverDialog);
  }

  function openServerEditor(index) {
    serverFormSnapshot = {
      name: serverName ? serverName.value : '',
      address: serverUrl ? serverUrl.value : ''
    };
    serverDialogCommitted = false;
    const hasServer = Number.isInteger(index) && index >= 0 && index < servers.length;
    editingServerIndex = hasServer ? index : -1;
    const server = hasServer ? servers[index] : null;
    if (serverDialogHeading) {
      serverDialogHeading.textContent = hasServer ? 'Edit Server' : 'Add Server';
    }
    if (serverDialogMode) {
      serverDialogMode.textContent = hasServer ? 'Saved Server' : 'New Server';
    }
    if (serverName) {
      serverName.value = server ? displayServerName(server) : '';
    }
    if (serverUrl) {
      serverUrl.value = server ? server.address : '';
    }
    if (removeServerButton) {
      removeServerButton.hidden = !hasServer;
      removeServerButton.disabled = !hasServer;
    }
    openDialog(serverDialog, serverName);
  }

  function saveServerFromForm() {
    if (!serverUrl) {
      return false;
    }
    const address = serverUrl.value.trim();
    if (!address) {
      setStatus('Enter a WebSocket URL');
      return false;
    }
    const entry = {
      name: serverName && serverName.value.trim()
        ? serverName.value.trim()
        : deriveServerName(address),
      address
    };

    let targetIndex = editingServerIndex;
    const existingIndex = servers.findIndex((server, index) =>
      server.address === address && index !== editingServerIndex);
    if (targetIndex >= 0 && targetIndex < servers.length) {
      servers[targetIndex] = entry;
    } else if (existingIndex >= 0) {
      servers[existingIndex] = entry;
      targetIndex = existingIndex;
    } else {
      servers.push(entry);
      targetIndex = servers.length - 1;
    }

    selectedServerIndex = targetIndex;
    editingServerIndex = -1;
    saveServerList();
    saveClientSettings(address);
    syncBrowserStorage('server list');
    setSelectedServer(selectedServerIndex);
    serverDialogCommitted = true;
    closeDialog(serverDialog);
    setStatus('Server saved');
    return true;
  }

  function removeSelectedServer() {
    const targetIndex = editingServerIndex >= 0 ? editingServerIndex : selectedServerIndex;
    if (targetIndex < 0 || targetIndex >= servers.length) {
      return;
    }
    servers.splice(targetIndex, 1);
    selectedServerIndex = servers.length === 0 ? -1 : Math.min(targetIndex, servers.length - 1);
    editingServerIndex = -1;
    saveServerList();
    syncBrowserStorage('server removed');
    if (selectedServerIndex >= 0) {
      setSelectedServer(selectedServerIndex);
    } else {
      if (serverName) {
        serverName.value = '';
      }
      if (serverUrl) {
        serverUrl.value = '';
      }
      renderServerList();
      updateSelectedServerLine();
    }
    serverDialogCommitted = true;
    closeDialog(serverDialog);
    setStatus('Server removed');
  }

  function loadClientSettings() {
    if (!serverUrl || !fileExists(clientConfigPath)) {
      loadServerList();
      return;
    }
    try {
      const text = FS.readFile(clientConfigPath, { encoding: 'utf8' });
      for (const line of text.split(/\r?\n/)) {
        const match = line.match(/^\s*server_url:\s*(.+?)\s*$/);
        if (!match) {
          continue;
        }
        const value = parseYamlString(match[1]);
        if (value) {
          serverUrl.value = value;
        }
        break;
      }
    } catch (error) {
      appendWebLog(`settings load failed: ${error}`);
    }
    loadServerList();
  }

  function saveClientSettings(url) {
    if (!storageMetadataReady || typeof FS === 'undefined') {
      return;
    }
    const text = [
      '# Arvane web client settings',
      `server_url: ${yamlString(url || defaultServerUrl)}`,
      `updated_at: ${yamlString(timestamp())}`,
      ''
    ].join('\n');
    try {
      FS.writeFile(clientConfigPath, text);
      appendWebLog(`settings saved for ${url || defaultServerUrl}`);
    } catch (error) {
      appendWebLog(`settings save failed: ${error}`);
    }
  }

  function syncBrowserStorage(reason) {
    if (typeof Module === 'undefined' ||
        !Module.arvWebStorageReady ||
        Module.arvWebStorageFlushing) {
      return;
    }
    if (typeof FS === 'undefined' || typeof FS.syncfs !== 'function') {
      return;
    }
    Module.arvWebStorageFlushing = true;
    FS.syncfs(false, function (error) {
      Module.arvWebStorageFlushing = false;
      if (error) {
        Module.arvWebStorageError = String(error);
        setStatus(`Storage sync failed: ${Module.arvWebStorageError}`);
        return;
      }
      appendWebLog(`storage synced: ${reason || 'manual'}`);
    });
  }

  function runtimeStorageReady() {
    return runtimeReady &&
      typeof Module !== 'undefined' &&
      Module.arvWebStorageReady &&
      typeof Module.ccall === 'function';
  }

  function updateControlState() {
    const storageReady = runtimeStorageReady();
    const busy = shellState === 'boot' || shellState === 'connecting';
    const hasServer = Boolean(activeServerAddress());
    if (connectButton) {
      connectButton.disabled = !storageReady || busy || !hasServer;
      connectButton.textContent = shellState === 'connecting' ? 'Joining...' : 'Join Server';
    }
    if (disconnectButton) {
      disconnectButton.hidden = shellState !== 'connecting';
      disconnectButton.disabled = !runtimeReady || shellState !== 'connecting';
    }
    if (editServerButton) {
      editServerButton.disabled = selectedServerIndex < 0 || selectedServerIndex >= servers.length;
    }
    if (saveProfileButton) {
      saveProfileButton.disabled = !storageReady;
    }
    if (pickSkinButton) {
      pickSkinButton.disabled = !storageReady;
    }
  }

  function refreshProfileFromRuntime() {
    if (runtimeStorageReady()) {
      Module.ccall('arv_web_refresh_profile', null, [], []);
    }
  }

  function profileInitial(name) {
    const trimmed = String(name || '').trim();
    return trimmed ? trimmed.charAt(0).toUpperCase() : 'P';
  }

  function updateProfileChrome(name) {
    const displayName = String(name || '').trim() || 'WebPlayer';
    if (profileDisplayName) {
      profileDisplayName.textContent = displayName;
    }
    if (profileAvatar) {
      profileAvatar.textContent = profileInitial(displayName);
    }
  }

  function saveProfileName(quiet) {
    if (!playerName || !runtimeStorageReady()) {
      if (!quiet) {
        setStatus('Runtime is not ready yet');
      }
      return false;
    }
    if (!profileDirty) {
      return true;
    }
    const name = playerName.value.trim() || 'WebPlayer';
    const saved = Module.ccall(
      'arv_web_set_player_name',
      'number',
      ['string'],
      [name]) !== 0;
    if (saved) {
      profileDirty = false;
      updateProfileChrome(name);
      syncBrowserStorage('profile');
    } else if (!quiet) {
      setStatus('Profile save failed');
    }
    return saved;
  }

  window.arvWebApplyProfileState = function (state) {
    const displayName = state && state.displayName ? state.displayName : 'WebPlayer';
    currentSkinHash = state && state.skinHash ? state.skinHash : '';
    if (playerName && !profileDirty) {
      playerName.value = displayName;
    }
    updateProfileChrome(displayName);
    if (skinModel && currentSkinHash) {
      skinModel.checked = Boolean(state.slimModel);
    }
    if (skinSummary) {
      skinSummary.textContent = currentSkinHash ? 'Custom skin' : 'Default skin';
    }
    if (skinDetail) {
      const model = skinModel && skinModel.checked ? 'Slim model' : 'Classic model';
      if (currentSkinHash) {
        skinDetail.textContent = `${state.skinWidth} x ${state.skinHeight} PNG, ${model}`;
      } else {
        skinDetail.textContent = model;
      }
    }
  };

  function setShellState(nextState) {
    const normalized = ['boot', 'connect', 'connecting', 'game'].includes(nextState)
      ? nextState
      : 'connect';
    shellState = normalized;
    document.body.dataset.arvState = normalized;
    document.body.classList.toggle('in-game', normalized === 'game');
    if (shellRoot) {
      shellRoot.dataset.arvState = normalized;
    }
    const showPanel = normalized !== 'game';
    if (!showPanel) {
      closeAllDialogs();
    }
    if (connectionPanel) {
      connectionPanel.hidden = !showPanel;
      connectionPanel.inert = !showPanel;
      connectionPanel.setAttribute('aria-hidden', showPanel ? 'false' : 'true');
      if (showPanel) {
        if (!connectionPanel.isConnected && panelParent && panelMarker) {
          panelParent.insertBefore(connectionPanel, panelMarker.nextSibling);
        }
      } else if (connectionPanel.isConnected) {
        connectionPanel.remove();
      }
    }
    updateControlState();
  }

  window.arvWebSetShellState = setShellState;
  window.arvWebSetConnectionPanelVisible = function (visible) {
    setShellState(visible ? 'connect' : 'game');
  };
  window.arvWebRuntimeReady = function () {
    runtimeReady = true;
    document.body.classList.add('runtime-ready');
    if (shellState === 'boot') {
      setShellState('connect');
    }
    setStatus(Module.arvWebStorageReady ? 'Ready (storage mounted)' : 'Ready (storage unavailable)');
    refreshProfileFromRuntime();
    updateControlState();
  };

  window.Module = {
    canvas,
    locateFile(path) {
      if (path === 'arv_web.wasm') {
        return `${path}?v=${runtimeCacheKey}`;
      }
      return path;
    },
    arvWebStorageReady: false,
    arvWebStorageError: '',
    arvWebStorageFlushing: false,
    preRun: [function () {
      if (typeof FS === 'undefined' || typeof IDBFS === 'undefined') {
        Module.arvWebStorageError = 'IDBFS is unavailable';
        setStatus(Module.arvWebStorageError);
        return;
      }

      setStatus('Mounting browser storage');
      Module.addRunDependency('arvane-idbfs');
      try {
        ensureDirectory('/arvane_home');
        FS.mount(IDBFS, {}, '/arvane_home');
      } catch (error) {
        Module.arvWebStorageError = String(error);
        setStatus(`Storage mount failed: ${Module.arvWebStorageError}`);
        Module.removeRunDependency('arvane-idbfs');
        return;
      }

      setStatus('Loading browser storage');
      FS.syncfs(true, function (loadError) {
        if (loadError) {
          Module.arvWebStorageError = String(loadError);
          setStatus(`Storage load failed: ${Module.arvWebStorageError}`);
          Module.removeRunDependency('arvane-idbfs');
          return;
        }

        try {
          ensureDirectory('/arvane_home/config');
          ensureDirectory('/arvane_home/cache');
          ensureDirectory('/arvane_home/cache/server-packs');
          ensureDirectory('/arvane_home/cache/skins');
          ensureDirectory('/arvane_home/imports');
          ensureDirectory('/arvane_home/keys');
          ensureDirectory('/arvane_home/logs');
          ensureDirectory('/arvane_home/skins');
        } catch (error) {
          Module.arvWebStorageError = String(error);
          setStatus(`Storage layout failed: ${Module.arvWebStorageError}`);
          Module.removeRunDependency('arvane-idbfs');
          return;
        }

        FS.syncfs(false, function (saveError) {
          if (saveError) {
            Module.arvWebStorageError = String(saveError);
            setStatus(`Storage save failed: ${Module.arvWebStorageError}`);
            Module.removeRunDependency('arvane-idbfs');
            return;
          }
          Module.arvWebStorageReady = true;
          Module.arvWebStorageError = '';
          storageMetadataReady = true;
          loadClientSettings();
          appendWebLog('browser storage ready');
          flushPendingLogs();
          setStatus('Browser storage ready');
          updateControlState();
          Module.removeRunDependency('arvane-idbfs');
        });
      });
    }],
    print(text) {
      setStatus(text);
    },
    printErr(text) {
      appendWebLog(text);
      setStatus('Browser runtime error');
      console.error(text);
    },
    setStatus,
    onAbort(error) {
      setStatus(`Runtime aborted: ${error}`);
    }
  };

  function loadRuntime() {
    const script = document.createElement('script');
    script.async = true;
    script.src = `arv_web.js?v=${runtimeCacheKey}`;
    script.onerror = function () {
      setStatus('Failed to load runtime script');
    };
    document.body.appendChild(script);
  }

  function joinActiveServer() {
    const url = activeServerAddress();
    if (!url) {
      setStatus('Enter a WebSocket URL');
      openServerEditor(-1);
      return;
    }
    if (!runtimeStorageReady()) {
      setStatus('Runtime is not ready yet');
      return;
    }
    if (!saveProfileName(true)) {
      return;
    }
    saveClientSettings(url);
    syncBrowserStorage('connect settings');
    setStatus('connecting WebSocket');
    setShellState('connecting');
    Module.ccall('arv_web_connect', null, ['string'], [url]);
  }

  document.querySelectorAll('.dialog-close').forEach(function (button) {
    button.addEventListener('click', function () {
      closeDialog(button.closest('dialog'));
    });
  });

  [profileDialog, serverDialog].forEach(function (dialog) {
    if (!dialog) {
      return;
    }
    dialog.addEventListener('click', function (event) {
      if (event.target === dialog) {
        closeDialog(dialog);
      }
    });
  });

  if (serverDialog) {
    serverDialog.addEventListener('close', function () {
      if (!serverDialogCommitted && serverFormSnapshot) {
        if (serverName) {
          serverName.value = serverFormSnapshot.name;
        }
        if (serverUrl) {
          serverUrl.value = serverFormSnapshot.address;
        }
        updateSelectedServerLine();
      }
      serverFormSnapshot = null;
      serverDialogCommitted = false;
      editingServerIndex = -1;
    });
  }

  if (profileButton) {
    profileButton.addEventListener('click', function () {
      openDialog(profileDialog, playerName);
    });
  }

  if (profileForm) {
    profileForm.addEventListener('submit', function (event) {
      event.preventDefault();
      if (saveProfileName(false)) {
        closeDialog(profileDialog);
      }
    });
  }

  if (playerName) {
    playerName.addEventListener('input', function () {
      profileDirty = true;
      updateProfileChrome(playerName.value);
    });
  }

  if (skinModel) {
    skinModel.addEventListener('change', function () {
      if (!runtimeStorageReady()) {
        setStatus('Runtime is not ready yet');
        return;
      }
      if (!currentSkinHash) {
        if (skinDetail) {
          skinDetail.textContent = skinModel.checked ? 'Slim model' : 'Classic model';
        }
        setStatus('Skin model saved for default skin');
        return;
      }
      const saved = Module.ccall(
        'arv_web_set_skin_model',
        'number',
        ['number'],
        [skinModel.checked ? 1 : 0]) !== 0;
      if (saved) {
        syncBrowserStorage('skin model');
      }
    });
  }

  if (pickSkinButton && skinFile) {
    pickSkinButton.addEventListener('click', function () {
      if (!runtimeStorageReady()) {
        setStatus('Runtime is not ready yet');
        return;
      }
      skinFile.click();
    });

    skinFile.addEventListener('change', async function () {
      const file = skinFile.files && skinFile.files[0];
      if (!file) {
        return;
      }
      if (!runtimeStorageReady()) {
        setStatus('Runtime is not ready yet');
        return;
      }
      try {
        ensureDirectory(importDirectoryPath);
        const importPath = `${importDirectoryPath}/selected-${Date.now()}.png`;
        const bytes = new Uint8Array(await file.arrayBuffer());
        FS.writeFile(importPath, bytes);
        const imported = Module.ccall(
          'arv_web_import_skin',
          'number',
          ['string', 'number'],
          [importPath, skinModel && skinModel.checked ? 1 : 0]) !== 0;
        if (imported) {
          syncBrowserStorage('skin import');
        }
      } catch (error) {
        setStatus(`Skin import failed: ${error}`);
      } finally {
        skinFile.value = '';
      }
    });
  }

  if (newServerButton) {
    newServerButton.addEventListener('click', function () {
      openServerEditor(-1);
    });
  }

  if (editServerButton) {
    editServerButton.addEventListener('click', function () {
      openServerEditor(selectedServerIndex);
    });
  }

  if (serverForm) {
    serverForm.addEventListener('submit', function (event) {
      event.preventDefault();
      saveServerFromForm();
    });
  }

  if (removeServerButton) {
    removeServerButton.addEventListener('click', removeSelectedServer);
  }

  if (serverList) {
    serverList.addEventListener('keydown', function (event) {
      if (event.key === 'Enter') {
        event.preventDefault();
        joinActiveServer();
        return;
      }
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') {
        return;
      }
      event.preventDefault();
      if (servers.length === 0) {
        return;
      }
      const direction = event.key === 'ArrowDown' ? 1 : -1;
      const current = selectedServerIndex >= 0 ? selectedServerIndex : 0;
      const next = (current + direction + servers.length) % servers.length;
      setSelectedServer(next);
      const activeOption = document.getElementById(`server-option-${next}`);
      if (activeOption) {
        activeOption.scrollIntoView({ block: 'nearest' });
      }
    });
  }

  if (connectButton) {
    connectButton.addEventListener('click', joinActiveServer);
  }

  if (disconnectButton) {
    disconnectButton.addEventListener('click', function () {
      if (typeof Module.ccall === 'function') {
        Module.ccall('arv_web_disconnect', null, [], []);
      }
      setShellState('connect');
      syncBrowserStorage('disconnect');
    });
  }

  window.addEventListener('error', function (event) {
    appendWebLog(event.message || 'browser runtime error');
    if (event.error) {
      console.error('[arv_web] browser error', event.error);
    }
    setStatus('Browser runtime error');
  });

  renderServerList();
  updateProfileChrome(playerName ? playerName.value : 'WebPlayer');
  updateSelectedServerLine();
  updateControlState();
  setStatus('Loading runtime');
  setShellState('boot');
  loadRuntime();
})();
