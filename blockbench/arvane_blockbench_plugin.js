let exportArvaneEntityAction;
let exportArvaneBlockAction;

Plugin.register('arvane_blockbench_plugin', {
  title: 'Voxel YAML Exporter',
  icon: 'save',
  author: 'hippo',
  description: 'Exports models for Arvane',
  version: '1.1.0',
  variant: 'both',

  onload() {
    const FACE_NAMES = ['north', 'south', 'east', 'west', 'up', 'down'];

    function round3(n) {
      return +Number(n || 0).toFixed(3);
    }

    function roundUv(uv) {
      if (!Array.isArray(uv)) return [0, 0, 0, 0];
      return [
        round3(uv[0]),
        round3(uv[1]),
        round3(uv[2]),
        round3(uv[3])
      ];
    }

    function stripExtension(name) {
      return String(name || '').replace(/\.[^/.]+$/, '');
    }

    function getProjectNameWithoutExtension() {
      const name = Project.name || 'model';
      const lastIndex = name.lastIndexOf('.');
      return lastIndex === -1 ? name : name.substring(0, lastIndex);
    }

    function getTextureFileName(texture, index) {
      if (!texture) return `texture_${index}.png`;

      // Prefer the display/imported texture name.
      if (texture.name) {
        return texture.name;
      }

      // Fall back to filesystem path basename when available.
      if (texture.path) {
        const parts = String(texture.path).replaceAll('\\', '/').split('/');
        return parts[parts.length - 1] || `texture_${index}.png`;
      }

      // Last-resort stable-ish fallback.
      if (texture.uuid) {
        return `${texture.uuid}.png`;
      }

      return `texture_${index}.png`;
    }

    function buildTextureInfo() {
      const textures = Array.isArray(Project.textures) ? Project.textures : [];

      const names = textures.map((texture, index) => {
        return getTextureFileName(texture, index);
      });

      const sizes = textures.map(texture => {
        return [
          texture?.uv_width || texture?.width || 16,
          texture?.uv_height || texture?.height || 16
        ];
      });

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

        if (texture.name) {
          byName.set(String(texture.name), index);
          byName.set(stripExtension(texture.name), index);
        }

        const fileName = names[index];
        byName.set(fileName, index);
        byName.set(stripExtension(fileName), index);
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

      // Current Blockbench CubeFace API provides getTexture().
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

      // Fallback for saved/internal face texture references.
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

    function extractCubeData(cube, textureInfo) {
      const data = {
        min: cube.from.map(round3),
        max: cube.to.map(round3)
      };

      for (const faceName of FACE_NAMES) {
        data[faceName] = extractFace(cube.faces?.[faceName], textureInfo);
      }

      data['inflate'] = round3(cube.inflate);

      return data;
    }

    function extractBoneData(bone, textureInfo) {
      const boxes = [];

      bone.children.forEach(child => {
        if (child instanceof Cube && child.export) {
          boxes.push(extractCubeData(child, textureInfo));
        }
      });

      return {
        name: bone.name,
        pivot: bone.origin.map(round3),
        boxes
      };
    }

    function extractAnimations() {
      const result = [];

      if (!Animator?.animations) {
        return result;
      }

      for (const anim of Animator.animations) {
        const keyframes = {};

        for (const [bone_id, animator] of Object.entries(anim.animators || {})) {
          const bone_name = animator.name || bone_id;

          for (const keyframe of animator.keyframes || []) {
            const time = keyframe.time;

            if (!keyframes[time]) {
              keyframes[time] = {};
            }

            const boneData = {};

            if (keyframe.channel === 'rotation') {
              const rotation = [
                keyframe.get('x'),
                keyframe.get('y'),
                keyframe.get('z')
              ];

              boneData.rotation_euler = rotation.map(n => round3(parseFloat(n)));
            }

            if (keyframe.channel === 'position') {
              const position = [
                keyframe.get('x'),
                keyframe.get('y'),
                keyframe.get('z')
              ];

              boneData.position = position.map(n => round3(parseFloat(n)));
            }

            if (keyframe.channel === 'scale') {
              const scale = [
                keyframe.get('x'),
                keyframe.get('y'),
                keyframe.get('z')
              ];

              boneData.scale = scale.map(n => round3(parseFloat(n)));
            }

            if (Object.keys(boneData).length > 0) {
              if (!keyframes[time][bone_name]) {
                keyframes[time][bone_name] = {};
              }

              Object.assign(keyframes[time][bone_name], boneData);
            }
          }
        }

        const sorted = Object.entries(keyframes)
          .sort((a, b) => parseFloat(a[0]) - parseFloat(b[0]));

        const kfs = sorted.map(([time, bones]) => ({
          time: parseFloat(time),
          bones: Object.entries(bones || {}).map(([name, props]) => ({
            name,
            ...props
          }))
        }));

        result.push({
          name: anim.name,
          length: round3(anim.length),
          loop: anim.loop,
          keyframes: kfs
        });
      }

      return result;
    }

    function addTextureFields(model, textureInfo) {
      model.textures = textureInfo.names;
      model.texture_sizes = textureInfo.sizes;

      return model;
    }

    exportArvaneEntityAction = new Action('export_arvane_entity_model', {
      name: 'Export Arvane Entity Model',
      description: 'Export an Arvane entity model as JSON',
      icon: 'save',
      click: () => {
        const textureInfo = buildTextureInfo();
        const skeleton = [];

        Outliner.root.forEach(node => {
          if (node instanceof Group && node.export) {
            skeleton.push(extractBoneData(node, textureInfo));
          }
        });

        const model = addTextureFields({
          name: getProjectNameWithoutExtension(),
          skeleton
        }, textureInfo);

        const animations = extractAnimations();
        if (animations.length > 0) {
          model.animations = animations;
        }

        Blockbench.export({
          type: 'json',
          name: `${getProjectNameWithoutExtension()}.json`,
          content: JSON.stringify(model, null, 2)
        });
      }
    });

    exportArvaneBlockAction = new Action('export_arvane_block_model', {
      name: 'Export Arvane Block Model',
      description: 'Export an Arvane block model as JSON',
      icon: 'save',
      click: () => {
        const textureInfo = buildTextureInfo();
        const boxes = [];

        Outliner.root.forEach(node => {
          if (node instanceof Cube && node.export) {
            boxes.push(extractCubeData(node, textureInfo));
          }
        });

        const model = addTextureFields({
          boxes
        }, textureInfo);

        Blockbench.export({
          type: 'json',
          name: `${getProjectNameWithoutExtension()}.json`,
          content: JSON.stringify(model, null, 2)
        });
      }
    });

    MenuBar.menus.file.addAction(exportArvaneEntityAction, 'export');
    MenuBar.menus.file.addAction(exportArvaneBlockAction, 'export');
  },

  onunload() {
    exportArvaneEntityAction?.delete();
    exportArvaneBlockAction?.delete();

    exportArvaneEntityAction = undefined;
    exportArvaneBlockAction = undefined;
  }
});