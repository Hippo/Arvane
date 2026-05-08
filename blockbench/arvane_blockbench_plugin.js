let exportArvaneModelAction;
let importArvaneModelAction;
let exportArvaneAnimationsAction;
let importArvaneAnimationsAction;

Plugin.register('arvane_blockbench_plugin', {
  title: 'Arvane Model Tools',
  icon: 'save',
  author: 'hippo',
  description: 'Exports and imports Arvane models and animation files',
  version: '2.2.0',
  variant: 'both',

  onload() {
    const FACE_NAMES = ['north', 'south', 'east', 'west', 'up', 'down'];
    const ANIMATION_CHANNELS = ['rotation', 'position', 'scale'];
    const DEFAULT_PIVOT = [0, 0, 0];

    function toNumber(value, fallback = 0) {
      const number = Number(value);
      return Number.isFinite(number) ? number : fallback;
    }

    function round3(value) {
      return +toNumber(value).toFixed(3);
    }

    function vec(value, length, fallback = 0) {
      const result = [];

      for (let i = 0; i < length; i += 1) {
        result.push(round3(Array.isArray(value) ? value[i] : fallback));
      }

      return result;
    }

    function roundUv(uv) {
      return vec(uv, 4, 0);
    }

    function normalizePath(path) {
      return String(path || '').replace(/\\/g, '/');
    }

    function baseName(path) {
      const normalized = normalizePath(path);
      const parts = normalized.split('/');
      return parts[parts.length - 1] || normalized || '';
    }

    function dirName(path) {
      const normalized = normalizePath(path);
      const slash = normalized.lastIndexOf('/');
      return slash === -1 ? '' : normalized.slice(0, slash);
    }

    function joinPath(directory, file) {
      if (!directory) return file;
      if (!file) return directory;
      return `${directory.replace(/\/$/, '')}/${file.replace(/^\//, '')}`;
    }

    function stripExtension(name) {
      return String(name || '').replace(/\.[^/.]+$/, '');
    }

    function getProjectNameWithoutExtension() {
      const name = typeof Project !== 'undefined' && Project.name ? Project.name : 'model';
      const lastIndex = name.lastIndexOf('.');
      return lastIndex === -1 ? name : name.substring(0, lastIndex);
    }

    function getProjectTextures() {
      if (typeof Project !== 'undefined' && Array.isArray(Project.textures)) {
        return Project.textures;
      }

      if (typeof Texture !== 'undefined' && Array.isArray(Texture.all)) {
        return Texture.all;
      }

      return [];
    }

    function getTextureFileName(texture, index) {
      if (!texture) return `texture_${index}.png`;

      if (texture.name) {
        return texture.name;
      }

      if (texture.path) {
        return baseName(texture.path) || `texture_${index}.png`;
      }

      if (texture.uuid) {
        return `${texture.uuid}.png`;
      }

      return `texture_${index}.png`;
    }

    function readTextureSize(texture) {
      return [
        toNumber(texture?.uv_width ?? texture?.width, 16),
        toNumber(texture?.uv_height ?? texture?.height, 16)
      ];
    }

    function buildTextureInfo() {
      const textures = getProjectTextures();

      const names = textures.map((texture, index) => getTextureFileName(texture, index));
      const sizes = textures.map(texture => readTextureSize(texture));

      const byUuid = new Map();
      const byName = new Map();
      const byObject = new Map();

      textures.forEach((texture, index) => {
        if (!texture) return;

        byObject.set(texture, index);

        if (texture.uuid) {
          byUuid.set(String(texture.uuid), index);
        }

        if (texture.id) {
          byUuid.set(String(texture.id), index);
        }

        const aliases = [
          texture.name,
          texture.path,
          names[index]
        ];

        aliases.forEach(alias => {
          if (!alias) return;

          const normalized = String(alias);
          const fileName = baseName(normalized);

          byName.set(normalized, index);
          byName.set(stripExtension(normalized), index);
          byName.set(fileName, index);
          byName.set(stripExtension(fileName), index);
        });
      });

      return {
        textures,
        names,
        sizes,
        byUuid,
        byName,
        byObject
      };
    }

    function getFaceTextureIndex(face, textureInfo) {
      if (!face || face.enabled === false) {
        return -1;
      }

      if (typeof face.getTexture === 'function') {
        const resolved = face.getTexture();

        if (resolved && textureInfo.byObject.has(resolved)) {
          return textureInfo.byObject.get(resolved);
        }

        if (resolved?.uuid && textureInfo.byUuid.has(String(resolved.uuid))) {
          return textureInfo.byUuid.get(String(resolved.uuid));
        }

        if (resolved?.id && textureInfo.byUuid.has(String(resolved.id))) {
          return textureInfo.byUuid.get(String(resolved.id));
        }

        if (resolved?.name) {
          const name = String(resolved.name);

          if (textureInfo.byName.has(name)) {
            return textureInfo.byName.get(name);
          }

          const stripped = stripExtension(name);

          if (textureInfo.byName.has(stripped)) {
            return textureInfo.byName.get(stripped);
          }
        }
      }

      if (face.texture !== false && face.texture != null) {
        const ref = String(face.texture);

        if (textureInfo.byUuid.has(ref)) {
          return textureInfo.byUuid.get(ref);
        }

        if (textureInfo.byName.has(ref)) {
          return textureInfo.byName.get(ref);
        }

        const stripped = stripExtension(ref);

        if (textureInfo.byName.has(stripped)) {
          return textureInfo.byName.get(stripped);
        }
      }

      return -1;
    }

    function extractFace(face, textureInfo) {
      return {
        texture: getFaceTextureIndex(face, textureInfo),
        uv: roundUv(face?.uv)
      };
    }

    function isExportedCube(node) {
      return typeof Cube !== 'undefined' && node instanceof Cube && node.export !== false;
    }

    function isExportedGroup(node) {
      return typeof Group !== 'undefined' && node instanceof Group && node.export !== false;
    }

    function getGroupPivot(group) {
      return vec(group?.origin || DEFAULT_PIVOT, 3, 0);
    }

    function extractCubeData(cube, textureInfo, boneName, pivot) {
      const data = {
        bone: String(boneName || ''),
        pivot: vec(pivot || DEFAULT_PIVOT, 3, 0),
        min: vec(cube.from, 3, 0),
        max: vec(cube.to, 3, 0),
        inflate: round3(cube.inflate)
      };

      for (const faceName of FACE_NAMES) {
        data[faceName] = extractFace(cube.faces?.[faceName], textureInfo);
      }

      return data;
    }

    function collectCubes(nodes, textureInfo, currentBoneName, currentPivot, out) {
      if (!Array.isArray(nodes)) return out;

      nodes.forEach(node => {
        if (isExportedCube(node)) {
          out.push(extractCubeData(node, textureInfo, currentBoneName, currentPivot));
          return;
        }

        if (isExportedGroup(node)) {
          collectCubes(
            node.children,
            textureInfo,
            node.name || '',
            getGroupPivot(node),
            out
          );
        }
      });

      return out;
    }

    function buildModelExport() {
      const textureInfo = buildTextureInfo();

      const boxes = collectCubes(
        typeof Outliner !== 'undefined' && Array.isArray(Outliner.root) ? Outliner.root : [],
        textureInfo,
        '',
        DEFAULT_PIVOT,
        []
      );

      return {
        textures: textureInfo.names,
        texture_sizes: textureInfo.sizes,
        boxes
      };
    }

    function getGroupById(id) {
      if (typeof Group === 'undefined' || !Array.isArray(Group.all)) {
        return null;
      }

      return Group.all.find(group => {
        return String(group.uuid) === String(id) || String(group.id) === String(id);
      }) || null;
    }

    function getGroupByName(name) {
      if (typeof Group === 'undefined' || !Array.isArray(Group.all)) {
        return null;
      }

      return Group.all.find(group => String(group.name) === String(name)) || null;
    }

    function getAnimatorBoneName(animator, boneId) {
      if (animator?.name) {
        return String(animator.name);
      }

      const group = getGroupById(boneId);

      if (group?.name) {
        return String(group.name);
      }

      return String(boneId);
    }

    function getKeyframeAxis(keyframe, axis) {
      if (keyframe && typeof keyframe.get === 'function') {
        return round3(parseFloat(keyframe.get(axis)));
      }

      const firstPoint = Array.isArray(keyframe?.data_points) ? keyframe.data_points[0] : null;

      if (firstPoint && firstPoint[axis] != null) {
        return round3(parseFloat(firstPoint[axis]));
      }

      return 0;
    }

    function getKeyframeValue(keyframe) {
      return [
        getKeyframeAxis(keyframe, 'x'),
        getKeyframeAxis(keyframe, 'y'),
        getKeyframeAxis(keyframe, 'z')
      ];
    }

    function getKeyframeInterpolation(keyframe) {
      return String(keyframe?.interpolation || keyframe?.easing || 'linear');
    }

    function getAnimations() {
      if (typeof Animator !== 'undefined' && Array.isArray(Animator.animations)) {
        return Animator.animations;
      }

      if (typeof Animation !== 'undefined' && Array.isArray(Animation.all)) {
        return Animation.all;
      }

      return [];
    }

    function buildAnimationExport() {
      const animations = [];

      getAnimations().forEach(animation => {
        const keyframes = [];

        Object.entries(animation.animators || {}).forEach(([boneId, animator]) => {
          const bone = getAnimatorBoneName(animator, boneId);

          (animator.keyframes || []).forEach(keyframe => {
            const channel = String(keyframe?.channel || '');

            if (!ANIMATION_CHANNELS.includes(channel)) {
              return;
            }

            keyframes.push({
              time: round3(keyframe.time),
              bone,
              channel,
              value: getKeyframeValue(keyframe),
              interpolation: getKeyframeInterpolation(keyframe)
            });
          });
        });

        keyframes.sort((a, b) => {
          if (a.time !== b.time) return a.time - b.time;
          if (a.bone !== b.bone) return a.bone.localeCompare(b.bone);
          return a.channel.localeCompare(b.channel);
        });

        animations.push({
          name: String(animation.name || 'animation'),
          length: round3(animation.length),
          loop: animation.loop ?? false,
          keyframes
        });
      });

      return { animations };
    }

    function exportArvaneModel() {
      const model = buildModelExport();
      const baseNameWithoutExtension = getProjectNameWithoutExtension();

      Blockbench.export({
        type: 'json',
        name: `${baseNameWithoutExtension}.json`,
        content: JSON.stringify(model, null, 2)
      });
    }

    function exportArvaneAnimations() {
      const animationFile = buildAnimationExport();

      Blockbench.export({
        type: 'json',
        name: 'animations.json',
        content: JSON.stringify(animationFile, null, 2)
      });
    }

    function parseJsonFile(file, label) {
      try {
        return JSON.parse(file.content);
      } catch (error) {
        throw new Error(`${label} is not valid JSON: ${error.message}`);
      }
    }

    function isVec3(value) {
      return Array.isArray(value)
        && value.length >= 3
        && value.slice(0, 3).every(component => Number.isFinite(Number(component)));
    }

    function assertArvaneModel(model) {
      if (!model || typeof model !== 'object' || Array.isArray(model)) {
        throw new Error('Arvane model must be a JSON object.');
      }

      if (!Array.isArray(model.textures)) {
        throw new Error('Arvane model must contain a textures array.');
      }

      if (!Array.isArray(model.texture_sizes)) {
        throw new Error('Arvane model must contain a texture_sizes array.');
      }

      if (!Array.isArray(model.boxes)) {
        throw new Error('Arvane model must contain a boxes array.');
      }

      model.boxes.forEach((box, index) => {
        if (!box || typeof box !== 'object' || Array.isArray(box)) {
          throw new Error(`Box at index ${index} must be an object.`);
        }

        if (typeof box.bone !== 'string') {
          throw new Error(`Box at index ${index} has invalid or missing bone field. Expected string.`);
        }

        if (!isVec3(box.pivot)) {
          throw new Error(`Box at index ${index} has invalid or missing pivot field. Expected numeric vec3.`);
        }

        if (!isVec3(box.min)) {
          throw new Error(`Box at index ${index} has invalid min field. Expected numeric vec3.`);
        }

        if (!isVec3(box.max)) {
          throw new Error(`Box at index ${index} has invalid max field. Expected numeric vec3.`);
        }
      });
    }

    function assertArvaneAnimations(animationFile) {
      if (!animationFile || typeof animationFile !== 'object' || Array.isArray(animationFile)) {
        throw new Error('Arvane animation file must be a JSON object.');
      }

      if (!Array.isArray(animationFile.animations)) {
        throw new Error('Arvane animation file must contain an animations array.');
      }
    }

    function findExistingTexture(name) {
      const candidates = [
        String(name || ''),
        baseName(name),
        stripExtension(String(name || '')),
        stripExtension(baseName(name))
      ].filter(Boolean);

      return getProjectTextures().find(texture => {
        const textureNames = [
          texture?.name,
          texture?.path,
          texture?.uuid,
          texture?.id
        ].filter(Boolean).flatMap(value => {
          const stringValue = String(value);
          const fileName = baseName(stringValue);

          return [
            stringValue,
            fileName,
            stripExtension(stringValue),
            stripExtension(fileName)
          ];
        });

        return candidates.some(candidate => textureNames.includes(candidate));
      }) || null;
    }

    function configureTextureSize(texture, size) {
      if (!texture) return;

      const width = toNumber(Array.isArray(size) ? size[0] : 16, 16);
      const height = toNumber(Array.isArray(size) ? size[1] : 16, 16);

      texture.uv_width = width;
      texture.uv_height = height;

      if (!texture.width) texture.width = width;
      if (!texture.height) texture.height = height;
    }

    function createImportedTexture(name, size, baseDirectory) {
      const existing = findExistingTexture(name);

      if (existing) {
        configureTextureSize(existing, size);
        return existing;
      }

      if (typeof Texture === 'undefined') {
        return null;
      }

      const textureName = baseName(name) || String(name || 'texture.png');
      const texture = new Texture({ name: textureName });
      const relativePath = String(name || '');
      const candidatePath = baseDirectory && relativePath ? joinPath(baseDirectory, relativePath) : '';

      if (candidatePath && typeof texture.fromPath === 'function') {
        try {
          texture.fromPath(candidatePath);
        } catch (error) {
          // Keep the named texture slot even when the image file is not available.
        }
      }

      configureTextureSize(texture, size);

      if (typeof texture.add === 'function') {
        texture.add(false);
      }

      return texture;
    }

    function getTextureReference(texture) {
      if (!texture) return false;
      return texture.uuid || texture.id || texture.name || false;
    }

    function applyImportedFace(cube, faceName, faceData, textures) {
      const face = cube.faces?.[faceName];

      if (!face) return;

      const data = faceData && typeof faceData === 'object' ? faceData : {};
      const textureIndex = Number(data.texture);

      face.uv = roundUv(data.uv);

      if (Number.isInteger(textureIndex) && textureIndex >= 0 && textures[textureIndex]) {
        face.texture = getTextureReference(textures[textureIndex]);

        if ('enabled' in face) {
          face.enabled = true;
        }
      } else {
        face.texture = false;

        if ('enabled' in face) {
          face.enabled = false;
        }
      }
    }

    function setGroupOrigin(group, pivot) {
      if (!group) return;

      const normalizedPivot = vec(pivot, 3, 0);
      group.origin = normalizedPivot;

      if (typeof group.transferOrigin === 'function') {
        try {
          group.transferOrigin(normalizedPivot);
        } catch (error) {
          // Some Blockbench formats do not support transferOrigin.
          group.origin = normalizedPivot;
        }
      }
    }

    function updateModelView() {
      if (typeof Canvas !== 'undefined' && typeof Canvas.updateAll === 'function') {
        Canvas.updateAll();
      }

      if (typeof Validator !== 'undefined' && typeof Validator.validate === 'function') {
        Validator.validate();
      }
    }

    function showImportError(error) {
      if (typeof Blockbench !== 'undefined' && typeof Blockbench.showMessageBox === 'function') {
        Blockbench.showMessageBox({
          title: 'Arvane Import Failed',
          message: error.message || String(error),
          icon: 'error'
        });
      } else {
        throw error;
      }
    }

    function showQuickMessage(message) {
      if (typeof Blockbench !== 'undefined' && typeof Blockbench.showQuickMessage === 'function') {
        Blockbench.showQuickMessage(message);
      }
    }

    function attachCubeToParent(cube, parent) {
      if (!parent) return cube;

      if (typeof cube.addTo === 'function') {
        cube.addTo(parent);
        return cube;
      }

      if (Array.isArray(parent.children) && !parent.children.includes(cube)) {
        parent.children.push(cube);
        cube.parent = parent;
      }

      return cube;
    }

    function sameVec3(a, b) {
      const av = vec(a, 3, 0);
      const bv = vec(b, 3, 0);

      return av[0] === bv[0] && av[1] === bv[1] && av[2] === bv[2];
    }

    function setGroupPivot(group, pivot) {
      if (!group) return;

      const normalizedPivot = vec(pivot || DEFAULT_PIVOT, 3, 0);

      if (Array.isArray(group.origin) && typeof group.origin.V3_set === 'function') {
        group.origin.V3_set(normalizedPivot);
      } else {
        group.origin = normalizedPivot.slice();
      }
    }

    function ensureModelGroup(name, pivot, importedGroupPivots) {
      const boneName = String(name || '');

      if (!boneName) {
        return null;
      }

      const normalizedPivot = vec(pivot || DEFAULT_PIVOT, 3, 0);
      const previousPivot = importedGroupPivots.get(boneName);

      if (previousPivot && !sameVec3(previousPivot, normalizedPivot)) {
        console.warn(
          `[Arvane] Bone '${boneName}' was imported with multiple pivots. ` +
          `Using first pivot ${JSON.stringify(previousPivot)}, ignoring ${JSON.stringify(normalizedPivot)}.`
        );
      }

      if (!previousPivot) {
        importedGroupPivots.set(boneName, normalizedPivot);
      }

      const existing = getGroupByName(boneName);

      if (existing) {
        if (!previousPivot) {
          setGroupPivot(existing, normalizedPivot);
        }

        return existing;
      }

      if (typeof Group === 'undefined') {
        throw new Error(`Cannot create group '${boneName}' because this Blockbench format does not expose groups.`);
      }

      const group = new Group({
        name: boneName,
        origin: normalizedPivot.slice()
      });

      setGroupPivot(group, normalizedPivot);

      if (typeof group.init === 'function') {
        group.init();
      }

      setGroupPivot(group, normalizedPivot);

      return group;
    }

    function createImportedCube(box, index, textures, importedGroupPivots) {
      if (!box || typeof box !== 'object' || Array.isArray(box)) {
        throw new Error(`Box at index ${index} must be an object.`);
      }

      if (!Array.isArray(box.min) || !Array.isArray(box.max)) {
        throw new Error(`Box at index ${index} must contain min and max arrays.`);
      }

      const cube = new Cube({
        name: box.name || `box_${index}`,
        from: vec(box.min, 3, 0),
        to: vec(box.max, 3, 0),
        inflate: round3(box.inflate)
      });

      FACE_NAMES.forEach(faceName => {
        applyImportedFace(cube, faceName, box[faceName], textures);
      });

      const parent = ensureModelGroup(
        box.bone || '',
        box.pivot || DEFAULT_PIVOT,
        importedGroupPivots
      );

      attachCubeToParent(cube, parent);

      if (typeof cube.init === 'function') {
        cube.init();
      }

      return cube;
    }

    function importArvaneModel(files) {
      try {
        const file = files?.[0];

        if (!file) return;

        const model = parseJsonFile(file, 'Arvane model');

        assertArvaneModel(model);

        const baseDirectory = dirName(file.path || '');
        const importedGroupPivots = new Map();

        const textures = model.textures.map((textureName, index) => {
          return createImportedTexture(textureName, model.texture_sizes[index], baseDirectory);
        });

        model.boxes.forEach((box, index) => {
          createImportedCube(box, index, textures, importedGroupPivots);
        });

        updateModelView();

        showQuickMessage(
          `Imported ${model.boxes.length} Arvane model box${model.boxes.length === 1 ? '' : 'es'}`
        );
      } catch (error) {
        showImportError(error);
      }
    }

    function requestArvaneModelImport() {
      Blockbench.import({
        resource_id: 'import_arvane_model',
        type: 'Arvane model',
        extensions: ['json'],
        readtype: 'text',
        multiple: false
      }, files => importArvaneModel(files));
    }

    function ensureAnimationGroup(name) {
      const boneName = String(name || '').trim();

      if (!boneName) {
        throw new Error('Animation keyframes must reference a non-empty bone name.');
      }

      const existing = getGroupByName(boneName);

      if (existing) return existing;

      if (typeof Group === 'undefined') {
        throw new Error(`Cannot create missing bone '${boneName}' because this Blockbench format does not expose groups.`);
      }

      const group = new Group({
        name: boneName,
        origin: DEFAULT_PIVOT.slice()
      });

      if (typeof group.init === 'function') {
        group.init();
      }

      return group;
    }

    function getGroupAnimatorKey(group) {
      return group.uuid || group.id || group.name;
    }

    function ensureBoneAnimator(animation, group) {
      if (!animation.animators) {
        animation.animators = {};
      }

      const key = getGroupAnimatorKey(group);

      if (animation.animators[key]) {
        return animation.animators[key];
      }

      if (typeof animation.getBoneAnimator === 'function') {
        const animator = animation.getBoneAnimator(group);

        if (animator) return animator;
      }

      if (typeof BoneAnimator !== 'undefined') {
        let animator = null;

        try {
          animator = new BoneAnimator(key, animation, group.name);
        } catch (firstError) {
          try {
            animator = new BoneAnimator(group.uuid, animation, group.name);
          } catch (secondError) {
            animator = null;
          }
        }

        if (animator) {
          animation.animators[key] = animator;
          return animator;
        }
      }

      throw new Error(`Could not create animator for bone '${group.name}'.`);
    }

    function createKeyframePayload(keyframeData) {
      const value = vec(keyframeData.value, 3, keyframeData.channel === 'scale' ? 1 : 0);

      return {
        channel: String(keyframeData.channel),
        time: round3(keyframeData.time),
        data_points: [{
          x: value[0],
          y: value[1],
          z: value[2]
        }],
        interpolation: String(keyframeData.interpolation || 'linear')
      };
    }

    function addKeyframeToAnimator(animator, keyframeData) {
      const payload = createKeyframePayload(keyframeData);

      if (typeof animator.addKeyframe === 'function') {
        return animator.addKeyframe(payload);
      }

      if (typeof Keyframe !== 'undefined') {
        const keyframe = new Keyframe(payload, animator);

        if (typeof keyframe.add === 'function') {
          return keyframe.add();
        }

        if (!Array.isArray(animator.keyframes)) {
          animator.keyframes = [];
        }

        animator.keyframes.push(keyframe);
        return keyframe;
      }

      if (!Array.isArray(animator.keyframes)) {
        animator.keyframes = [];
      }

      animator.keyframes.push(payload);
      return payload;
    }

    function createImportedAnimation(animationData) {
      if (typeof Animation === 'undefined') {
        throw new Error('This Blockbench format does not expose the Animation API.');
      }

      const animation = new Animation({
        name: String(animationData.name || 'animation'),
        length: round3(animationData.length),
        loop: animationData.loop ?? false
      });

      if (typeof animation.add === 'function') {
        animation.add(false);
      }

      if (!Array.isArray(animationData.keyframes)) {
        throw new Error(`Animation '${animation.name}' must contain a keyframes array.`);
      }

      animationData.keyframes.forEach((keyframe, index) => {
        if (!keyframe || typeof keyframe !== 'object' || Array.isArray(keyframe)) {
          throw new Error(`Keyframe ${index} in animation '${animation.name}' must be an object.`);
        }

        const boneName = String(keyframe.bone || '').trim();
        const channel = String(keyframe.channel || '');

        if (!boneName) {
          throw new Error(`Keyframe ${index} in animation '${animation.name}' is missing a bone name.`);
        }

        if (!ANIMATION_CHANNELS.includes(channel)) {
          throw new Error(
            `Keyframe ${index} in animation '${animation.name}' has unsupported channel '${channel}'.`
          );
        }

        const group = ensureAnimationGroup(boneName);
        const animator = ensureBoneAnimator(animation, group);

        addKeyframeToAnimator(animator, keyframe);
      });

      if (typeof animation.select === 'function') {
        animation.select();
      }

      return animation;
    }

    function importArvaneAnimations(files) {
      try {
        const file = files?.[0];

        if (!file) return;

        const animationFile = parseJsonFile(file, 'Arvane animation file');

        assertArvaneAnimations(animationFile);

        animationFile.animations.forEach(animationData => {
          createImportedAnimation(animationData);
        });

        updateModelView();

        showQuickMessage(
          `Imported ${animationFile.animations.length} Arvane animation${animationFile.animations.length === 1 ? '' : 's'}`
        );
      } catch (error) {
        showImportError(error);
      }
    }

    function requestArvaneAnimationsImport() {
      Blockbench.import({
        resource_id: 'import_arvane_animations',
        type: 'Arvane animations',
        extensions: ['json'],
        readtype: 'text',
        multiple: false
      }, files => importArvaneAnimations(files));
    }

    exportArvaneModelAction = new Action('export_arvane_model', {
      name: 'Export Arvane Model',
      description: 'Export the current model using the shared Arvane block/entity model schema.',
      icon: 'save',
      click: () => exportArvaneModel()
    });

    importArvaneModelAction = new Action('import_arvane_model', {
      name: 'Import Arvane Model',
      description: 'Import an Arvane model using the shared block/entity model schema.',
      icon: 'folder_open',
      click: () => requestArvaneModelImport()
    });

    exportArvaneAnimationsAction = new Action('export_arvane_animations', {
      name: 'Export Arvane Animations',
      description: 'Export Blockbench animation keyframes to animations.json. Keyframes reference bones by name.',
      icon: 'movie',
      click: () => exportArvaneAnimations()
    });

    importArvaneAnimationsAction = new Action('import_arvane_animations', {
      name: 'Import Arvane Animations',
      description: 'Import animations.json and bind keyframes to bones by name.',
      icon: 'folder_open',
      click: () => requestArvaneAnimationsImport()
    });

    MenuBar.menus.file.addAction(exportArvaneModelAction, 'export');
    MenuBar.menus.file.addAction(exportArvaneAnimationsAction, 'export');

    MenuBar.menus.file.addAction(importArvaneModelAction, 'import');
    MenuBar.menus.file.addAction(importArvaneAnimationsAction, 'import');
  },

  onunload() {
    exportArvaneModelAction?.delete();
    importArvaneModelAction?.delete();
    exportArvaneAnimationsAction?.delete();
    importArvaneAnimationsAction?.delete();

    exportArvaneModelAction = undefined;
    importArvaneModelAction = undefined;
    exportArvaneAnimationsAction = undefined;
    importArvaneAnimationsAction = undefined;
  }
});
