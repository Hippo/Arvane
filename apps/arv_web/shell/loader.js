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
  const removeServerButton = document.getElementById('remove-server-button');
  const selectedServerLine = document.getElementById('selected-server-line');
  const profileForm = document.getElementById('profile-form');
  const playerName = document.getElementById('player-name');
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
  const defaultServers = [{ name: 'Local', address: defaultServerUrl }];
  const pendingLogLines = [];
  const runtimeCacheKey = String(Date.now());
  let storageMetadataReady = false;
  let runtimeReady = false;
  let shellState = 'boot';
  let panelParent = connectionPanel ? connectionPanel.parentNode : null;
  let panelMarker = null;
  let servers = defaultServers.slice();
  let selectedServerIndex = 0;
  let profileDirty = false;
  let currentSkinHash = '';

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

  function setStatus(text) {
    if (String(text || '').trim() === '') {
      return;
    }
    if (statusLine) {
      statusLine.textContent = text;
    }
    appendWebLog(text);
    console.log('[arv_web]', text);
  }

  window.arvWebSetStatus = setStatus;

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

  function deriveServerName(address) {
    try {
      const parsed = new URL(address);
      if (parsed.hostname === 'localhost' || parsed.hostname === '127.0.0.1') {
        return 'Local';
      }
      return parsed.hostname || 'Server';
    } catch (error) {
      const compact = String(address || '').replace(/^wss?:\/\//, '').split('/')[0];
      return compact || 'Server';
    }
  }

  function activeServerAddress() {
    return serverUrl ? serverUrl.value.trim() : defaultServerUrl;
  }

  function updateSelectedServerLine() {
    if (selectedServerLine) {
      selectedServerLine.textContent = activeServerAddress() || 'No server selected';
    }
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
      if (removeServerButton) {
        removeServerButton.disabled = true;
      }
      return;
    }

    servers.forEach((server, index) => {
      const entry = document.createElement('button');
      entry.type = 'button';
      entry.className = 'server-entry';
      entry.setAttribute('role', 'option');
      entry.setAttribute('aria-selected', index === selectedServerIndex ? 'true' : 'false');

      const copy = document.createElement('div');
      const title = document.createElement('strong');
      title.textContent = server.name;
      const address = document.createElement('span');
      address.textContent = server.address;
      copy.append(title, address);

      const pill = document.createElement('span');
      pill.className = 'server-pill';
      pill.textContent = index === selectedServerIndex ? 'Selected' : 'Saved';

      entry.append(copy, pill);
      entry.addEventListener('click', function () {
        setSelectedServer(index);
      });
      serverList.appendChild(entry);
    });

    if (removeServerButton) {
      removeServerButton.disabled = selectedServerIndex < 0;
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
    const configuredUrl = activeServerAddress();
    const configuredIndex = servers.findIndex((server) => server.address === configuredUrl);
    if (configuredIndex >= 0) {
      setSelectedServer(configuredIndex);
    } else {
      selectedServerIndex = -1;
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

    const existingIndex = servers.findIndex((server) => server.address === address);
    if (selectedServerIndex >= 0 && selectedServerIndex < servers.length) {
      servers[selectedServerIndex] = entry;
    } else if (existingIndex >= 0) {
      servers[existingIndex] = entry;
      selectedServerIndex = existingIndex;
    } else {
      servers.push(entry);
      selectedServerIndex = servers.length - 1;
    }

    saveServerList();
    saveClientSettings(address);
    syncBrowserStorage('server list');
    setSelectedServer(selectedServerIndex);
    setStatus('Server saved');
    return true;
  }

  function removeSelectedServer() {
    if (selectedServerIndex < 0 || selectedServerIndex >= servers.length) {
      return;
    }
    servers.splice(selectedServerIndex, 1);
    selectedServerIndex = servers.length === 0 ? -1 : Math.min(selectedServerIndex, servers.length - 1);
    saveServerList();
    syncBrowserStorage('server removed');
    if (selectedServerIndex >= 0) {
      setSelectedServer(selectedServerIndex);
    } else {
      if (serverName) {
        serverName.value = '';
      }
      renderServerList();
      updateSelectedServerLine();
    }
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
    if (connectButton) {
      connectButton.disabled = !storageReady || busy;
    }
    if (disconnectButton) {
      disconnectButton.disabled = !runtimeReady || shellState === 'boot';
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
      syncBrowserStorage('profile');
    } else if (!quiet) {
      setStatus('Profile save failed');
    }
    return saved;
  }

  function shortSkinHash(hash) {
    if (!hash) {
      return '';
    }
    return `${hash.slice(0, 8)}...${hash.slice(-6)}`;
  }

  window.arvWebApplyProfileState = function (state) {
    const displayName = state && state.displayName ? state.displayName : 'WebPlayer';
    currentSkinHash = state && state.skinHash ? state.skinHash : '';
    if (playerName && !profileDirty) {
      playerName.value = displayName;
    }
    if (skinModel && currentSkinHash) {
      skinModel.checked = Boolean(state.slimModel);
    }
    if (skinSummary) {
      skinSummary.textContent = currentSkinHash ? 'Custom skin' : 'Default skin';
    }
    if (skinDetail) {
      const model = skinModel && skinModel.checked ? 'Slim model' : 'Classic model';
      if (currentSkinHash) {
        skinDetail.textContent = `${state.skinWidth}x${state.skinHeight} PNG, ${model}, ${shortSkinHash(currentSkinHash)}`;
      } else {
        skinDetail.textContent = `${model}; choose a PNG to replace it.`;
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
      setStatus(text);
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

  if (profileForm) {
    profileForm.addEventListener('submit', function (event) {
      event.preventDefault();
      saveProfileName(false);
    });
  }

  if (playerName) {
    playerName.addEventListener('input', function () {
      profileDirty = true;
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
          skinDetail.textContent = `${skinModel.checked ? 'Slim model' : 'Classic model'}; choose a PNG to replace it.`;
        }
        setStatus('Skin model will apply to the next imported skin');
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
      selectedServerIndex = -1;
      if (serverName) {
        serverName.value = '';
        serverName.focus();
      }
      if (serverUrl) {
        serverUrl.value = '';
      }
      renderServerList();
      updateSelectedServerLine();
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

  if (serverUrl) {
    serverUrl.addEventListener('input', function () {
      const matchingIndex = servers.findIndex((server) => server.address === serverUrl.value.trim());
      selectedServerIndex = matchingIndex;
      if (matchingIndex >= 0 && serverName) {
        serverName.value = servers[matchingIndex].name;
      }
      renderServerList();
      updateSelectedServerLine();
    });
  }

  if (connectButton) {
    connectButton.addEventListener('click', function () {
      const url = activeServerAddress();
      if (!url) {
        setStatus('Enter a WebSocket URL');
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
      setShellState('connecting');
      Module.ccall('arv_web_connect', null, ['string'], [url]);
    });
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
    setStatus(event.message || 'Browser runtime error');
  });

  renderServerList();
  updateSelectedServerLine();
  updateControlState();
  setStatus('Loading runtime');
  setShellState('boot');
  loadRuntime();
})();
