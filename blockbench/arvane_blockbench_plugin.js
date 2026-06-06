let exportArvaneModelAction;
let importArvaneModelAction;
let exportArvaneAnimationsAction;
let importArvaneAnimationsAction;

Plugin.register('arvane_blockbench_plugin', {
  title: 'Arvane Model Tools',
  icon: 'save',
  author: 'hippo',
  description: 'Exports and imports Arvane models and animation files',
  version: '2.3.0',
  variant: 'both',

  onload() {
    const FACE_NAMES = ['north', 'south', 'east', 'west', 'up', 'down'];
    const ANIMATION_CHANNELS = ['rotation', 'position', 'scale'];
    const DEFAULT_PIVOT = [0, 0, 0];
    const DEFAULT_ROTATION = [0, 0, 0];

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

    function vec2(value, fallback = 0) {
      return vec(value, 2, fallback);
    }

    function vec3(value, fallback = 0) {
      return vec(value, 3, fallback);
    }

    function roundUv(uv) {
      return vec(uv, 4, 0);
    }

    function sameVec3(a, b) {
      const av = vec3(a, 0);
      const bv = vec3(b, 0);

      return av[0] === bv[0] && av[1] === bv[1] && av[2] === bv[2];
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
        uv: roundUv(face?.uv),
        rotation: round3(face?.rotation)
      };
    }

    function isExportedCube(node) {
      return typeof Cube !== 'undefined' && node instanceof Cube && node.export !== false;
    }

    function isExportedGroup(node) {
      return typeof Group !== 'undefined' && node instanceof Group && node.export !== false;
    }

    function getParentBoneName(group) {
      const parent = group?.parent;

      if (isExportedGroup(parent) && parent.name) {
        return String(parent.name);
      }

      return '';
    }

    function addVec3IfNonDefault(target, key, value, defaultValue = DEFAULT_PIVOT) {
      const normalized = vec3(value, 0);

      if (!sameVec3(normalized, defaultValue)) {
        target[key] = normalized;
      }
    }

    function extractBoneData(group) {
      const data = {};
      const parent = getParentBoneName(group);

      addVec3IfNonDefault(data, 'pivot', group?.origin || DEFAULT_PIVOT, DEFAULT_PIVOT);
      addVec3IfNonDefault(data, 'rotation', group?.rotation || DEFAULT_ROTATION, DEFAULT_ROTATION);

      if (parent) {
        data.parent = parent;
      }

      return data;
    }

    function extractCubeData(cube, textureInfo, boneName) {
      const data = {
        bone: String(boneName || ''),
        min: vec3(cube.from, 0),
        max: vec3(cube.to, 0),
        inflate: round3(cube.inflate)
      };

      addVec3IfNonDefault(data, 'origin', cube.origin || DEFAULT_PIVOT, DEFAULT_PIVOT);
      addVec3IfNonDefault(data, 'rotation', cube.rotation || DEFAULT_ROTATION, DEFAULT_ROTATION);

      if (Array.isArray(cube.uv_offset) && !sameVec3([cube.uv_offset[0], cube.uv_offset[1], 0], DEFAULT_PIVOT)) {
        data.uv_offset = vec2(cube.uv_offset, 0);
      }

      if (cube.box_uv === true) {
        data.box_uv = true;
      }

      if (cube.mirror_uv === true) {
        data.mirror_uv = true;
      }

      if (cube.shade === false) {
        data.shade = false;
      }

      for (const faceName of FACE_NAMES) {
        data[faceName] = extractFace(cube.faces?.[faceName], textureInfo);
      }

      return data;
    }

    function recordBone(bones, group) {
      const boneName = String(group?.name || '');

      if (!boneName) return '';

      const data = extractBoneData(group);

      if (Object.prototype.hasOwnProperty.call(bones, boneName)) {
        console.warn(`[Arvane] Duplicate bone name '${boneName}' encountered while exporting. Bone names should be unique.`);
      }

      bones[boneName] = data;
      return boneName;
    }

    function collectModelNodes(nodes, textureInfo, currentBoneName, bones, boxes) {
      if (!Array.isArray(nodes)) return;

      nodes.forEach(node => {
        if (isExportedCube(node)) {
          if (!currentBoneName) {
            throw new Error(`Cube '${node.name || node.uuid || 'unnamed'}' must be inside an exported group so it can reference a bone.`);
          }
          boxes.push(extractCubeData(node, textureInfo, currentBoneName));
          return;
        }

        if (isExportedGroup(node)) {
          const boneName = recordBone(bones, node);
          collectModelNodes(node.children, textureInfo, boneName || currentBoneName, bones, boxes);
        }
      });
    }

    function buildModelExport() {
      const textureInfo = buildTextureInfo();
      const bones = {};
      const boxes = [];

      collectModelNodes(
        typeof Outliner !== 'undefined' && Array.isArray(Outliner.root) ? Outliner.root : [],
        textureInfo,
        '',
        bones,
        boxes
      );

      return {
        textures: textureInfo.names,
        texture_sizes: textureInfo.sizes,
        bones,
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

    function isOptionalVec3(value) {
      return value == null || isVec3(value);
    }

    function isOptionalVec4(value) {
      return value == null || (
        Array.isArray(value)
        && value.length >= 4
        && value.slice(0, 4).every(component => Number.isFinite(Number(component)))
      );
    }

    function isOptionalUvRotation(value) {
      if (value == null) return true;

      const number = Number(value);
      if (!Number.isFinite(number)) return false;

      const normalized = ((number % 360) + 360) % 360;
      return Math.abs(normalized - Math.round(normalized / 90) * 90) <= 0.001;
    }

    function isObjectRecord(value) {
      return value != null && typeof value === 'object' && !Array.isArray(value);
    }

    function assertArvaneModel(model) {
      if (!isObjectRecord(model)) {
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

      if (!isObjectRecord(model.bones)) {
        throw new Error('Arvane model must contain a bones object.');
      }

      Object.entries(model.bones).forEach(([boneName, bone], index) => {
        if (!boneName) {
          throw new Error(`Bone at index ${index} has an empty name.`);
        }

        if (!isObjectRecord(bone)) {
          throw new Error(`Bone '${boneName || index}' must be an object.`);
        }

        if (!isOptionalVec3(bone.pivot)) {
          throw new Error(`Bone '${boneName || index}' has invalid pivot field. Expected numeric vec3.`);
        }

        if (!isOptionalVec3(bone.rotation)) {
          throw new Error(`Bone '${boneName || index}' has invalid rotation field. Expected numeric vec3.`);
        }

        if (bone.parent != null && typeof bone.parent !== 'string') {
          throw new Error(`Bone '${boneName || index}' has invalid parent field. Expected string.`);
        }

        if (bone.parent != null && !Object.prototype.hasOwnProperty.call(model.bones, bone.parent)) {
          throw new Error(`Bone '${boneName || index}' references unknown parent '${bone.parent}'.`);
        }
      });

      model.boxes.forEach((box, index) => {
        if (!isObjectRecord(box)) {
          throw new Error(`Box at index ${index} must be an object.`);
        }

        if (typeof box.bone !== 'string') {
          throw new Error(`Box at index ${index} has invalid or missing bone field. Expected string.`);
        }

        if (!Object.prototype.hasOwnProperty.call(model.bones, box.bone)) {
          throw new Error(`Box at index ${index} references unknown bone '${box.bone}'.`);
        }

        if (box.pivot != null) {
          throw new Error(`Box at index ${index} uses removed pivot field. Use box.origin for cube pivots and bones.<name>.pivot for bone pivots.`);
        }

        if (!isVec3(box.min)) {
          throw new Error(`Box at index ${index} has invalid min field. Expected numeric vec3.`);
        }

        if (!isVec3(box.max)) {
          throw new Error(`Box at index ${index} has invalid max field. Expected numeric vec3.`);
        }

        if (!isOptionalVec3(box.origin)) {
          throw new Error(`Box at index ${index} has invalid origin field. Expected numeric vec3.`);
        }

        if (!isOptionalVec3(box.rotation)) {
          throw new Error(`Box at index ${index} has invalid rotation field. Expected numeric vec3.`);
        }

        if (box.uv_offset != null && !Array.isArray(box.uv_offset)) {
          throw new Error(`Box at index ${index} has invalid uv_offset field. Expected numeric vec2.`);
        }

        FACE_NAMES.forEach(faceName => {
          const face = box[faceName];
          if (face == null) return;

          if (!isObjectRecord(face)) {
            throw new Error(`Box at index ${index} face '${faceName}' must be an object.`);
          }

          if (!isOptionalVec4(face.uv)) {
            throw new Error(`Box at index ${index} face '${faceName}' has invalid uv field. Expected numeric vec4.`);
          }

          if (!isOptionalUvRotation(face.rotation)) {
            throw new Error(`Box at index ${index} face '${faceName}' has invalid rotation field. Expected numeric 0/90/180/270 degrees.`);
          }
        });
      });
    }

    function assertArvaneAnimations(animationFile) {
      if (!isObjectRecord(animationFile)) {
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

    function getFaceRotation(faceData) {
      if (!faceData || typeof faceData !== 'object') return 0;
      return round3(faceData.rotation);
    }

    function applyImportedFace(cube, faceName, faceData, textures) {
      const face = cube.faces?.[faceName];

      if (!face) return;

      const data = isObjectRecord(faceData) ? faceData : {};
      const textureIndex = Number(data.texture);

      face.uv = roundUv(data.uv);
      face.rotation = getFaceRotation(data);

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

    function setArrayVector(target, key, value, length, fallback = 0) {
      if (!target) return;

      const normalized = vec(value, length, fallback);

      if (Array.isArray(target[key]) && typeof target[key].V3_set === 'function') {
        target[key].V3_set(normalized);
        return;
      }

      if (Array.isArray(target[key]) && typeof target[key].V2_set === 'function') {
        target[key].V2_set(normalized);
        return;
      }

      target[key] = normalized.slice();
    }

    function setGroupPivot(group, pivot) {
      setArrayVector(group, 'origin', pivot || DEFAULT_PIVOT, 3, 0);
    }

    function setGroupRotation(group, rotation) {
      setArrayVector(group, 'rotation', rotation || DEFAULT_ROTATION, 3, 0);
    }

    function setCubeOrigin(cube, origin) {
      setArrayVector(cube, 'origin', origin || DEFAULT_PIVOT, 3, 0);
    }

    function setCubeRotation(cube, rotation) {
      setArrayVector(cube, 'rotation', rotation || DEFAULT_ROTATION, 3, 0);
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

    function attachNodeToParent(node, parent) {
      if (!node || !parent) return node;

      if (typeof node.addTo === 'function') {
        node.addTo(parent);
        return node;
      }

      if (Array.isArray(parent.children) && !parent.children.includes(node)) {
        parent.children.push(node);
        node.parent = parent;
      }

      return node;
    }

    function getModelBoneData(model, boneName) {
      const bone = model.bones[String(boneName || '')];
      return isObjectRecord(bone) ? bone : {};
    }

    function getBoneParentName(boneData) {
      return typeof boneData?.parent === 'string' ? boneData.parent.trim() : '';
    }

    function getBonePivot(model, boneName) {
      const boneData = getModelBoneData(model, boneName);
      return vec3(boneData.pivot || DEFAULT_PIVOT, 0);
    }

    function getBoneRotation(model, boneName) {
      const boneData = getModelBoneData(model, boneName);
      return vec3(boneData.rotation || DEFAULT_ROTATION, 0);
    }

    function ensureModelGroup(name, model, importedGroups, creatingGroups) {
      const boneName = String(name || '');

      if (!boneName) {
        throw new Error('Arvane boxes must reference a non-empty declared bone.');
      }

      if (!Object.prototype.hasOwnProperty.call(model.bones, boneName)) {
        throw new Error(`Unknown Arvane bone '${boneName}'.`);
      }

      if (importedGroups.has(boneName)) {
        return importedGroups.get(boneName);
      }

      if (creatingGroups.has(boneName)) {
        throw new Error(`Bone hierarchy cycle detected at bone '${boneName}'.`);
      }

      if (typeof Group === 'undefined') {
        throw new Error(`Cannot create group '${boneName}' because this Blockbench format does not expose groups.`);
      }

      creatingGroups.add(boneName);

      const boneData = getModelBoneData(model, boneName);
      const parentName = getBoneParentName(boneData);
      const parentGroup = parentName
        ? ensureModelGroup(parentName, model, importedGroups, creatingGroups)
        : null;
      const pivot = getBonePivot(model, boneName);
      const rotation = getBoneRotation(model, boneName);
      const existing = getGroupByName(boneName);
      const group = existing || new Group({
        name: boneName,
        origin: pivot.slice(),
        rotation: rotation.slice()
      });

      setGroupPivot(group, pivot);
      setGroupRotation(group, rotation);

      if (!existing && typeof group.init === 'function') {
        group.init();
      }

      setGroupPivot(group, pivot);
      setGroupRotation(group, rotation);
      attachNodeToParent(group, parentGroup);

      importedGroups.set(boneName, group);
      creatingGroups.delete(boneName);

      return group;
    }

    function getBoxOrigin(box) {
      return vec3(box.origin || DEFAULT_PIVOT, 0);
    }

    function getBoxRotation(box) {
      return vec3(box.rotation || DEFAULT_ROTATION, 0);
    }

    function createImportedCube(box, index, model, textures, importedGroups, creatingGroups) {
      if (!isObjectRecord(box)) {
        throw new Error(`Box at index ${index} must be an object.`);
      }

      if (!Array.isArray(box.min) || !Array.isArray(box.max)) {
        throw new Error(`Box at index ${index} must contain min and max arrays.`);
      }

      const origin = getBoxOrigin(box);
      const rotation = getBoxRotation(box);
      const cube = new Cube({
        name: box.name || `box_${index}`,
        from: vec3(box.min, 0),
        to: vec3(box.max, 0),
        inflate: round3(box.inflate),
        origin: origin.slice(),
        rotation: rotation.slice(),
        box_uv: box.box_uv === true,
        mirror_uv: box.mirror_uv === true,
        shade: box.shade !== false,
        uv_offset: Array.isArray(box.uv_offset) ? vec2(box.uv_offset, 0) : undefined
      });

      setCubeOrigin(cube, origin);
      setCubeRotation(cube, rotation);

      FACE_NAMES.forEach(faceName => {
        applyImportedFace(cube, faceName, box[faceName], textures);
      });

      const parent = ensureModelGroup(
        box.bone,
        model,
        importedGroups,
        creatingGroups
      );

      attachNodeToParent(cube, parent);

      if (typeof cube.init === 'function') {
        cube.init();
      }

      setCubeOrigin(cube, origin);
      setCubeRotation(cube, rotation);

      return cube;
    }

    function importArvaneModel(files) {
      try {
        const file = files?.[0];

        if (!file) return;

        const model = parseJsonFile(file, 'Arvane model');

        assertArvaneModel(model);

        const baseDirectory = dirName(file.path || '');
        const importedGroups = new Map();
        const creatingGroups = new Set();

        const textures = model.textures.map((textureName, index) => {
          return createImportedTexture(textureName, model.texture_sizes[index], baseDirectory);
        });

        // Create declared bones before cubes so hierarchy, pivots, and rotations exist even for empty bones.
        Object.keys(model.bones).forEach(boneName => {
          ensureModelGroup(boneName, model, importedGroups, creatingGroups);
        });

        model.boxes.forEach((box, index) => {
          createImportedCube(box, index, model, textures, importedGroups, creatingGroups);
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
        origin: DEFAULT_PIVOT.slice(),
        rotation: DEFAULT_ROTATION.slice()
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
      const value = vec3(keyframeData.value, keyframeData.channel === 'scale' ? 1 : 0);

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
        if (!isObjectRecord(keyframe)) {
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
