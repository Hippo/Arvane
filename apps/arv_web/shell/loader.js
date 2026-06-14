(function () {
  const statusLine = document.getElementById('status-line');
  const canvas = document.getElementById('arvane-canvas');
  const shellRoot = document.querySelector('.web-client');
  const connectionPanel = document.querySelector('.session-panel');
  const connectButton = document.getElementById('connect-button');
  const disconnectButton = document.getElementById('disconnect-button');
  const serverUrl = document.getElementById('server-url');
  const clientConfigPath = '/arvane_home/config/client.yaml';
  const sessionLogPath = '/arvane_home/logs/web-session.log';
  const defaultServerUrl = serverUrl ? serverUrl.value : 'ws://localhost:42070/arvane';
  const pendingLogLines = [];
  const runtimeCacheKey = String(Date.now());
  let storageMetadataReady = false;
  let shellState = 'boot';
  let panelParent = connectionPanel ? connectionPanel.parentNode : null;
  let panelMarker = null;

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

  function loadClientSettings() {
    if (!serverUrl || !fileExists(clientConfigPath)) {
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
        return;
      }
    } catch (error) {
      appendWebLog(`settings load failed: ${error}`);
    }
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
  }

  window.arvWebSetShellState = setShellState;
  window.arvWebSetConnectionPanelVisible = function (visible) {
    setShellState(visible ? 'connect' : 'game');
  };
  window.arvWebRuntimeReady = function () {
    document.body.classList.add('runtime-ready');
    if (shellState === 'boot') {
      setShellState('connect');
    }
    setStatus(Module.arvWebStorageReady ? 'Ready (storage mounted)' : 'Ready (storage unavailable)');
  };

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
          ensureDirectory('/arvane_home/logs');
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

  connectButton.addEventListener('click', function () {
    const url = serverUrl.value.trim();
    if (!url) {
      setStatus('Enter a WebSocket URL');
      return;
    }
    saveClientSettings(url);
    syncBrowserStorage('connect settings');
    if (typeof Module.ccall === 'function') {
      setShellState('connecting');
      Module.ccall('arv_web_connect', null, ['string'], [url]);
    } else {
      setShellState('connect');
      setStatus('Runtime is not ready yet');
    }
  });

  disconnectButton.addEventListener('click', function () {
    if (typeof Module.ccall === 'function') {
      Module.ccall('arv_web_disconnect', null, [], []);
    }
    setShellState('connect');
    syncBrowserStorage('disconnect');
  });

  window.addEventListener('error', function (event) {
    setStatus(event.message || 'Browser runtime error');
  });

  setStatus('Loading runtime');
  setShellState('boot');
  loadRuntime();
})();
