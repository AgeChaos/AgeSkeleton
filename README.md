# AgeSkeleton

![AgeSkeleton](misc/ageskeleton/icon.svg)

独立的 **2D 骨骼动画编辑器**，用于制作骨架、绑定图片与蒙皮、调整权重、编辑关键帧动画并导出资源。基于 Godot 源码开发。

A standalone **2D skeletal animation editor** built on Godot, with rigging, skinning, weight painting, keyframe animation and resource export.

无需账号登录或在线授权。原创编辑器与运行库代码、图标采用免费使用与限制商业化许可，允许自行修改与内部使用；禁止对外商业发行、服务及商业产品集成，无论是否开源；第三方组件保留各自许可证。

## 功能 / Features

- **骨架 / Rigging**：骨骼层级、姿态编辑、IK。
- **蒙皮 / Skinning**：图片附件、网格细分、自动绑定、权重笔刷。
- **动画 / Animation**：时间轴、属性关键帧、插值曲线、播放与指定时间预览。
- **组织 / Organization**：皮肤、插槽、附件与绘制顺序。
- **工作区 / Workspace**：画布、动画片段、时间轴、层级树、属性可独立拖动停靠、合并页签或浮动；布局保存在本机编辑器设置中。
- **导入导出 / Import and export**：Spine JSON 导入、工程保存及资源导出；不代表兼容 Spine 的所有版本和特性。
- **AI 操作 / AI authoring**：本地结构化接口，支持创建、编辑、验证、保存及截图；不内置大模型。

当前构建脚本面向 **Windows x64**。其他平台源码和历史模板仍保留，但不代表各平台已完成运行验收。

## 原创示例 / Original sample

仓库提供 **Moonwing（月翼龙）**：原创矢量贴图、12 根骨骼、10 个蒙皮附件及 Soar／Glide 两段循环动画，采用 MIT 许可证。翼膜包含渐变权重，可用于练习蒙皮与动画。素材、骨架与运动来自 `tools/generate_moonwing.py`，来源记录见 `samples/moonwing/provenance.json`。

```powershell
python tools/open_editor.py --project samples/moonwing
```

Moonwing includes original vector artwork, a 12-bone rig, ten skinned attachments and two looping animations under MIT. Open the project, switch to **Animate**, select **Soar** or **Glide**, then press Play. The left toolbar selects editing tools; transform values and diamond keyframe buttons are in the right inspector. Animation clips sit beside the timeline.

左侧工具栏用于选择、移动、旋转、缩放、建骨与权重绘制；右侧属性区编辑变换并用菱形按钮插入关键帧。切换到顶栏的“动画”模式，在底部动画列表选择 Soar 或 Glide 后播放。

拖动面板页签到另一面板边缘可分割停靠，拖到中间可合并，拖出工作区可成为独立窗口。页签最右侧三个点提供关闭、浮动和最大化；顶栏“面板”菜单可重新打开面板或重置布局。旋转工具拖动圆环；缩放工具拖动轴端方块调整单轴，拖动中心方块等比缩放。

选中图片，在“蒙皮与约束”勾选“编辑网格”。默认“调整网格”同步修改顶点、UV 和蒙皮权重，避免固定 UV 造成的贴图拉伸；外轮廓移动会改变裁剪范围。需要主动拉伸图片时选择“变形网格”。Esc 取消当前拖动，撤销/重做同时恢复几何、UV 和权重。调整模式拒绝三角形翻折；原 UV 位于图片范围内时，也会阻止拖出图片范围。

属性面板跟随当前选择：骨骼显示变换、长度和 IK；图片显示变换和蒙皮；插槽显示附件、颜色、顺序和混合；皮肤与占位符显示各自的组合或绑定工具。笔刷参数仅在启用权重笔刷后显示。选中皮肤仅查看属性，点击“应用皮肤”才切换角色外观。插槽、皮肤、占位符的名称栏用于显示实际名称，不会修改所属骨架名称；皮肤和占位符使用各自的重命名按钮。

The inspector follows the selected object: bone transforms and IK, image transforms and skinning, slot appearance and draw order, or skin/placeholder bindings. Brush settings appear only while weight painting is enabled. Selecting a skin inspects it; **Apply skin** changes the active appearance. Virtual item name fields display their own names and cannot accidentally rename the skeleton.

变换数值（旋转、位置、缩放、倾斜）支持横向拖调：鼠标悬浮显示左右箭头，按住左键左右拖动；Shift 微调，Esc 或右键取消，轻点进入文字输入。拖动实时预览，松手提交一次修改，可一次撤销；动画模式仍遵循自动关键帧开关。

Transform values support horizontal scrubbing: drag left/right, hold Shift for fine adjustment, or press Esc/right-click to cancel. Click without dragging to type. A drag previews live and commits one undoable change on release; animation editing respects Auto Key.

选中图片 → **蒙皮与约束 → 自动轮廓**，可按透明度生成网格边缘。默认轮廓简化 **4 像素**、边缘留白 **2 像素**；简化值越大，边缘点越少，留白越大，轮廓离图片越远（受原网格覆盖范围限制）。工具会去掉旧三角形产生的多余分割点，保留间隔适当的内部控制点并插值蒙皮权重，支持一次撤销恢复原网格。含逐帧替换网格动画的附件暂不允许自动重建。

**Auto Contour:** select an image → Skinning and Constraints → Auto Contour. The defaults are 4 px simplification and 2 px padding. Increase simplification for fewer vertices, or padding for more room around the image, within the original mesh footprint. Spaced interior controls are retained and skin weights are interpolated. The operation is undoable; attachments with frame-replacement mesh tracks cannot be rebuilt.

Enable **Edit mesh** on an image attachment. **Adjust mesh** updates texture coordinates and interpolates skin weights; moving the boundary changes the crop. **Deform mesh** intentionally keeps UVs fixed to stretch the image. Escape cancels a drag; undo/redo restores geometry, UVs and weights together. Adjust mode rejects folded triangles and out-of-image UVs for normalized image mappings.

Drag a panel tab to an edge to split, to the center to merge, or outside the workspace to float. The three-dot menu controls each panel; **Panels** reopens closed panes and resets the layout. Drag the rotation ring, the scale axis squares, or the central uniform-scale handle.

发行打包使用明确文件清单，只收录编辑器、许可及 Moonwing 示例，不扫描本机工作区、历史案例或导出目录：

```powershell
python tools/package_release.py --output ../AgeSkeletonData/Exports/AgeSkeleton-Windows.zip
```

The package includes a SHA-256 inventory and opens Moonwing by default. Packaging does not change a development build into an optimized release build.

## 多部位换装 / Outfit composition

原创 **Wayfarer（旅人）** 示例包含 11 根骨骼、23 个蒙皮附件，以及 Idle／Walk 动画：

```powershell
python tools/open_editor.py --project samples/wardrobe
```

选中角色或其骨骼，在右侧属性的 **换装** 区域选择上衣、裤装、头饰和武器。各部位独立组合；更换上衣会同时替换身体与两只袖子。选择“默认”恢复基础服装，头饰和武器则卸下。换装支持撤销、重做、保存，并保留动画播放状态。

Select the character or a bone, then use **Wardrobe** in the inspector to combine tops, bottoms, headwear and weapons. Changing a top replaces its body and both sleeves together. **Default** restores the base outfit or removes headwear/weapons. Changes support undo, redo and saving without interrupting animation playback.

制作自己的服装时，使用 `分组/款式` 命名皮肤，例如 `Tops/Scout Tunic`。同一分组代表互斥选项，不同分组可以组合；一套皮肤可以为多个插槽的同名占位符提供附件。各套附件需要绑定到共用骨架。换装复用现有 `active_skins` 皮肤组合数据，无需复制骨架或动画。旧工程没有分组皮肤时，不显示此区域。

Name skins `Group/Variant` to define parts. Each group selects one variant; a variant can supply attachments for several slot placeholders. Bind replacement meshes to the shared rig. Composition uses the existing `active_skins` data. The panel appears only when grouped skins exist.

本地 AI 接口也支持换装；空 `skin` 恢复该部位默认值 / Local AI operation (an empty `skin` restores the default part):

```json
{"operation":"wardrobe","entity":0,"group":"Tops","skin":"Tops/Steel Armor"}
```

示例由 `tools/generate_wardrobe.py` 生成，素材与骨架采用 MIT；来源见 `samples/wardrobe/provenance.json`。当前发行打包清单仍只包含 Moonwing。

The sample artwork and rig are original and MIT licensed. The current release packaging manifest still includes only Moonwing.

## 跨引擎运行库 / Cross-engine runtimes (0.4)

源码位于 `Runtimes/`。**AgeChaos 使用原生 ECS 骨骼；Unity、Unreal、Cocos 和原版 Godot 使用采样网格格式 `ageskeleton.meshclip` v2（读取兼容 v1）。** 后者保存各帧顶点位置，运行时线性插值，支持动画播放／暂停／跳转／变速、皮肤组合与插槽显隐。导出采样保留原有 IK 和蒙皮结果。v2 额外保存骨骼矩阵和逐顶点蒙皮影响，支持运行时 CCD IK 目标及带整数／浮点／字符串参数的动画事件。支持指定时长的动画混合过渡；任意骨骼编辑仍未实现。资源大小随顶点数、动画时长和采样率增长。它不是完整骨骼运行时的等价替代，也没有跨引擎性能领先的保证。

AgeChaos retains native ECS skeletal evaluation. The other engines use sampled vertex clips: IK and skinning are baked at export, then vertices interpolate during playback. Version 2 adds sampled bone matrices and per-influence local coordinates for runtime CCD IK targets, plus typed animation events. V1 reading remains supported. Timed animation crossfades are supported; arbitrary bone editing remains unavailable. Size scales with vertices, duration and sampling rate. No performance superiority is implied.

在编辑器的 **工程 → 导出** 中选择 **引擎运行库**，再选择 **Unity / Unreal / Cocos / Godot** 页签，指定一个尚不存在的目录。输出 `skeleton.ageskel.json` 和 PNG 图集页面，包含全部动画及皮肤。导出不改变当前姿态；现有目录不会被覆盖。当前格式只接受单骨架、普通透明混合；双色着色和乒乓循环会明确拒绝，不会静默丢失。也可通过本地会话导出：

```powershell
python tools/export_runtime.py <session.json> <new-output-directory> --entity 0 --fps 30
```

Use **Project → Export → Engine runtime → Unity / Unreal / Cocos / Godot**, or the CLI above. Export into a new directory and keep the JSON and PNG pages together. V2 accepts a single skeleton and normal alpha blending; unsupported two-color tint and ping-pong clips are rejected explicitly.

图集打包复用原生 `SpriteAtlasBuilder`，不缩放、不裁剪、不旋转原图片，页面最大 2048，边缘扩展 2 像素。AgeChaos 导出为压缩的 `SharedTextures/*.res`，外部引擎导出为 PNG。超出页面限制的多图片源会明确报错，已经只有一张源纹理时保持原尺寸。原始 UV 须在 0..1 内。

四个外部运行库均按附件顺序合批，**不会把 A → B → A 重排为 A → A → B**；多页面、材质差异和透明顺序仍可能产生多个批次。此处是角色内合批，不保证多个角色合为一次绘制。Wayfarer 的 23 张原图打包为 2 页（2048×2048、256×2048）；选用服装决定 1 或 3 个批次，Unity 原生图集二次打包后本案例为 1 批。当前动画仍为 JSON 采样数据，未实现二进制动画压缩。

The shared native atlas builder preserves source pixels and UVs, with 2-pixel edge extrusion and 2048-pixel pages. External runtimes merge only contiguous attachments on the same texture, preserving A/B/A transparency order. Multiple pages can still require multiple batches; this is per-character batching. Animation data remains sampled JSON, not a compressed binary format.

| Target | Integration | Validation in this iteration |
| --- | --- | --- |
| Unity 2022.3.15f1c1 / 6000.3.8f1 | `Runtimes/Unity` UPM package | Runtime IK/events and callbacks, direct/native atlas rendering and pixel comparison in both editors |
| Godot 4.4+ | `Runtimes/Godot` C++ GDExtension | Windows x64 build, IK/event signals and Godot 4.4.1 rendering |
| Unreal Engine 5 | `Runtimes/Unreal/AgeSkeleton` C++ plugin | Shared native core tested; UE build/device validation pending |
| Cocos Creator 3.x | `Runtimes/Cocos` TypeScript component | Official 3.8.8 type check and evaluator tests; engine rendering pending |
| AgeChaos | Built-in `ECSSkeleton2D` / `ECSWorld`, no additional package | Native compact ECS/shared-texture save-load and animation regression; platform/device rendering not revalidated |

**Unity**：Package Manager → Add package from disk，选择 `Runtimes/Unity/package.json`。将导出文件复制到 `Assets`，选中 JSON，执行 **Tools → AgeSkeleton → Create Player From Selected JSON**，然后运行场景。导入工具会绑定 PNG 并生成材质；相机应朝向角色的 XY 平面。可通过 `AgeSkeletonPlayer.Play("Walk")`、`SetWardrobe("Tops", "Tops/Steel Armor")` 控制。材质须保留资源引用，避免构建时裁剪 shader。角色内按绘制顺序合并连续使用同一实际纹理的附件，姿态更新复用网格数组；用 `BatchCount` 查看批次数。需要 Unity 原生图集时，选择 **Create Player With Native Sprite Atlas**：创建 Sprite Atlas V2，设置为完整矩形、无旋转、无裁剪，并启用项目的 Sprite Atlas V2 模式。也可给组件手动指定 `spriteAtlas`。页面精灵名称须与导出页面文件名（不含扩展名）一致。运行时按原生图集的 UV 重映射，多个导出页面被装入同一原生页面时可继续合批。

Unity: install the UPM package from disk, copy exported data into Assets, select the JSON and use the Tools menu to create a player. View its XY plane with a camera. The importer creates a material reference so the shader is included in builds. Contiguous attachments sharing an actual texture are merged; `BatchCount` reports the result. The **Create Player With Native Sprite Atlas** menu creates full-rectangle, untrimmed page sprites and a Sprite Atlas V2, enabling V2 packing for the project. Assign a native `spriteAtlas` manually if preferred; sprite names must match exported page basenames. Atlas UVs are remapped automatically. Cross-character batching is not provided.

**Godot**：使用官方 `godot-cpp` 4.4 构建（SDK 不随本仓库复制）：

```powershell
cmake -S Runtimes/Godot -B <build-directory> -DGODOT_CPP_PATH=<godot-cpp-4.4-directory>
cmake --build <build-directory> --config Release
```

将 `Runtimes/Godot/addons/ageskeleton` 复制到 Godot 项目；把动态库放入其 `bin/` 下。添加 `AgeSkeletonPlayer` 节点，设置 `source` 为 JSON 路径，通过 `play("Walk")`、`set_wardrobe(...)` 控制。GDScript 只负责调用，求值与绘制均为 C++。JSON 需要在 Godot 导出预设的非资源文件过滤器中包含（例如 `*.json`），PNG 也需随包导出。当前已构建 Windows x64 DLL；Linux/macOS 入口已列出但需自行编译，移动端/Web 未交付可用二进制。

Godot: build against godot-cpp 4.4, copy the addon and matching library to the project, then add `AgeSkeletonPlayer`. Set `source` and call its exposed methods. Include JSON and PNG files when exporting the Godot project. Only Windows x64 was built and tested here; other platforms require matching builds.

**UE5**：用 `tools/package_runtimes.py` 生成源码包，将解压后的 `AgeSkeleton` 放进项目 `Plugins/`，生成并编译 C++ 工程。在 Actor 上添加 `AgeSkeletonComponent`。导入 PNG，按 JSON 的 `textures` 顺序填入 `TexturePages`。创建双面、Unlit、Translucent 材质：`MainTexture` 纹理参数 × Vertex Color，RGB 接 Emissive，A 接 Opacity；赋给 `BaseMaterial`。`JsonFile` 指向 Content 下的 JSON，设置 `InitialAnimation`。蓝图提供 Play／Pause／Resume／Seek／SetWardrobe／SetSlotVisible 等。角色使用 XZ 平面。打包时将 JSON 目录加入 Additional Non-Asset Directories to Package；纹理、材质使用组件资源引用。当前无 UE5 环境，插件还需在 UE5 实际编译与渲染验收。

UE5: package and copy the plugin to Plugins, compile, add the actor component, assign texture pages and an unlit two-sided translucent material with `MainTexture` multiplied by vertex color. JSON paths are relative to Content; stage that directory for packaged games. Blueprint controls are provided. The character lies on XZ. UE5 build/render validation is still pending.

**Cocos**：将 `Runtimes/Cocos/*.ts` 和导出资源复制到 Creator 项目的 `assets`。给场景 Node 添加 `AgeSkeletonPlayer`，绑定 JsonAsset 和按顺序排列的 Texture2D，设置初始动画。使用普通 3D 相机查看 XY 平面；相机的可见层应包含角色。默认创建透明 unlit 材质，也可赋自定义材质。脚本可调用 `play`、`setWardrobe`、`setSlotVisible`。组件采用 Creator 3.8 动态网格 API；其他 3.x 小版本须验证 API，不能视为已全部兼容。

可选绑定组件的 `spriteAtlas`，页面 SpriteFrame 名称与导出页面文件名（不含扩展名）匹配，必须关闭旋转和透明裁剪。组件用 SpriteFrame 区域重映射 UV，并按最终纹理合批；加载后若图集布局改变需重新 `load()`。使用默认朝向 XY 平面的相机，批次通过微小 Z 偏移排序；任意相机方向及角色交错排序仍需项目验证。

Cocos native SpriteAtlas is optional. Use page-basename SpriteFrames, with rotation and trimming disabled. Reload after atlas layout changes. The component remaps UVs and merges consecutive attachments sharing the final texture. Transparent batch ordering uses small local Z offsets for the standard XY-plane camera.

Cocos: copy the TypeScript files and assets into a Creator project, attach the component, assign the JSON and textures in export order, and view the XY plane with a 3D camera. The adapter uses the 3.8 dynamic mesh API; older 3.x minors and actual engine rendering remain to be validated.

**AgeChaos**：直接使用引擎自带的 `ECSSkeleton2D` / `ECSWorld`，无需安装额外运行库或 `NativePlayer`。在 AgeSkeleton 中选择“引擎运行库 → AgeChaos”，导出原生 `.res` 与 `SharedTextures`，资源和图集一起放入游戏项目并加载到 ECS 实体。骨骼与蒙皮由原生 ECS 更新，不使用通用网格采样数据。

```csharp
// world and skeletonEntity belong to the game. Keep this controller while in use.
var skeleton = world.GetSkeletonController(skeletonEntity);
skeleton.Play("Walk");
skeleton.SetSkin("Tops/Steel Armor");
skeleton.SetSlotAttachment("Weapon", ""); // Clear the current attachment.
// Before destroying the entity/world, or unloading the game script:
skeleton.Dispose();
```

组合皮肤通过 `world.SetSkeleton2D` 设置 `active_skins`；跳转时间通过 `world.SetAnimation` 设置 `time`。这些是现有原生组件接口，无需创建另一个播放器。清空插槽附件不等于持久的强制隐藏，后续动画附件关键帧仍可改变它。

AgeChaos: use the built-in `ECSSkeleton2D` / `ECSWorld` directly; no separate runtime package or player wrapper is required. Export native `.res` assets with SharedTextures. Dispose the controller before destroying its entity/world. Use `SetSkeleton2D` with `active_skins` for skin composition and `SetAnimation` with `time` for seeking. Animation attachment keys can override a manual attachment change.

其他引擎源码包生成（Unity、Cocos、Unreal、Godot；AgeChaos 无需包） / Source archives for external engines (AgeChaos needs no package):

```powershell
python tools/package_runtimes.py --output <new-package-output-directory>
```

Windows 验收不代表 Android、iOS、Web 或其他平台验收通过。运行库原创部分采用与编辑器相同的限制商业化许可，禁止商业游戏集成；原生解析器保留 `Runtimes/Native/third_party/LICENSE.json`，Godot C++ 动态库还须随附 godot-cpp 的许可证。

Windows validation does not certify Android, iOS, Web or other platforms. Original runtime code uses the same restricted commercialization license as the editor; commercial integration is prohibited; preserve the JSON parser and godot-cpp notices when distributing native libraries.

### 动画混合过渡 / Animation crossfades

```csharp
// Unity: fade from the current pose into Walk over 0.3 animation seconds.
player.CrossFade("Walk", 0.3f);
```

Godot 使用 `player.cross_fade("Walk", 0.3)`；UE5 使用 `CrossFade` 蓝图节点；Cocos 使用 `player.crossFade('Walk', 0.3)`。Unity／Cocos 提供 `IsBlending`／`isBlending` 和 `BlendProgress`／`blendProgress`；Godot 为 `is_blending()`、`get_blend_progress()`，UE5 为 `IsBlending()`、`GetBlendProgress()`。默认过渡 0.2 秒，传入 0 立即切换；`restart=false` 仅在目标就是当前动画时保留其时间。

- 正常过渡期间，源动画与目标动画都继续播放。过渡中再次切换，以切换瞬间的混合姿态为固定源，避免突跳和无限叠加旧动画。
- 暂停、速度为 0 时冻结过渡；倒放仍向目标完成过渡。过渡时钟乘以播放速度绝对值。例如时长 0.3、速度 2，在现实时间 0.15 秒完成。
- 顶点和插槽颜色连续混合；附件选择／绘制顺序在权重达到 50% 时切换。手动皮肤、附件和隐藏设置仍然优先。
- 只派发目标动画事件；IK 在混合姿态之后求解。暂停、Seek 等原有事件规则保持有效。
- `Play`、`Stop`、`Seek` 取消当前过渡。非循环目标先到末尾时，保持末尾姿态直到过渡完成，再停止。
- v1／v2 已导出数据均可使用，无须重导出。此处为采样网格的线性过渡，不是多轨道、骨骼遮罩或加法动画层；大幅反向姿态的顶点插值可能产生收缩，需要缩短过渡或增加中间动作。

The outgoing and incoming clips advance together during a crossfade. Interrupted fades capture the current raw blended pose, so switches stay continuous. Fade time scales with absolute playback speed and freezes on pause or zero speed. Discrete slot keys/orders switch at 50%; manual overrides still win. Only destination events fire, and IK runs after blending. Play/Stop/Seek cancel the fade; a short non-looping destination holds its endpoint until the fade completes. Existing v1/v2 exports work unchanged. This is linear sampled-mesh crossfading, not layered/additive or masked skeletal blending; large opposing poses may shrink during interpolation.

### 运行时 IK 与事件 / Runtime IK and events

- Unity：`SetIKTarget(bone, x, y, chainLength, mix)` / `ClearIKTarget(bone)` / `GetBoneTip(bone)`；订阅 `AnimationEvent`。
- Godot C++：`set_ik_target(bone, Vector2, chain_length, mix)` / `clear_ik_target` / `get_bone_tip`；连接 `animation_event(name, payload)` 信号（延迟派发，payload 含 animation/time/int/float/string）。
- UE5：`SetIKTarget` / `ClearIKTarget` / `GetBoneTip` 蓝图接口；`OnAnimationEvent` 多播事件。
- Cocos：`setIKTarget` / `clearIKTarget` / `getBoneTip`；监听节点 `ageskeleton-event`。

坐标统一为**导出骨架本地像素，Y 向下**，不是游戏世界坐标；引擎的 pixelsPerUnit 不改变这些接口的坐标。骨骼按导出名称选择，链长为 1–16 且不得超过祖先链；mix 为 0–1。更改目标即时更新蒙皮，清除目标恢复采样姿态。重叠 IK 链按导出骨骼顺序求解。v1 数据没有实时 IK 信息，需要重新导出。

事件保留原始关键帧时间，支持正放、倒放及跨多轮循环；暂停、零步长和跳转不补发事件。重新播放后的第一次非零更新包含起点事件；循环边界同时存在尾帧和首帧事件时两者均触发。一次更新超过 4096 次事件会拒绝该步，保持时间不变；应使用较小步长。事件仅在相应次更新派发，修改皮肤或 IK 不触发事件。

IK targets use exported skeleton-local pixels (Y down), independent of world transforms. Events preserve authored times and typed payloads; forward/reverse looping is supported. Pause, zero steps and seek do not emit skipped events. More than 4096 event occurrences in one update rejects the step atomically. V1 needs re-export for IK and events. Crossfades are supported; arbitrary bone editing remains unavailable.

## 直接使用 / Run the existing build

编译完成后，打开项目内的 `bin/` 目录，运行：

```text
AgeSkeleton.exe
```

双击该程序即可启动。顶部模式按钮（骨架／动画）位于工程菜单之前；通过 **工程 → 打开项目目录** 打开当前 `.ageskeleton` 所在文件夹；新工程需要先保存。如果使用源码构建，可在源码根目录运行以下命令；示例数据目录须与编译时一致，并已包含构建产物和工程。

Run these commands from the source checkout:

```powershell
$env:AGESKELETON_DATA_ROOT = [System.IO.Path]::GetFullPath("../AgeSkeletonData")
python tools/open_editor.py
```

打开其他已有工程 / Open another project:

```powershell
python tools/open_editor.py --project "../MyProjects/MySkeleton/MySkeleton.ageskeleton"
```

新工程只需一个 `.ageskeleton` 文件和 `images/` 目录，不需要 `project.godot`。旧工作区仍可以通过 `--project <旧工作区目录>` 打开并另存为新格式。`AgeSkeleton.console.exe` 是带控制台输出的伴随启动器，须与 `AgeSkeleton.exe` 放在同一目录。

### 独立工程文件 / Portable documents

```text
MyCharacter/
    MyCharacter.ageskeleton
    images/
        body.png
        head.png
```

工程文件包含骨骼、插槽、皮肤、权重与动画数据；图片保存在 `images/`，使用相对路径。搬移或分享时带上整个文件夹。保存时不覆盖同名但内容不同的图片；删除附件后不会自动删除图片，以免丢失素材。旧 `.tres` 工程请在原工作区打开，再通过“另存为”转换。

The compressed document stores rig, slot, skin, weight and animation data; PNG images are referenced relative to the document. Move/share the entire folder. Conflicting image files are preserved, and unused images are not deleted automatically. Open legacy `.tres` files in their original workspace before saving a portable copy.

Windows 双击打开及文件图标注册（当前用户，无需管理员）：

```powershell
python tools/register_file_type.py
```

Register the current build for `.ageskeleton` files and its file icon using the command above. If you move the editor executable, run it again with `--binary <new path>`. Windows may ask you to choose AgeSkeleton if another application already owns a user-selected default.

### 基本制作流程 / Authoring workflow

1. 在工程菜单中新建或打开骨骼工程。
2. 在设置模式下建立骨骼层级，加入图片附件。
3. 将图片绑定到骨骼；需要变形时细分网格并调整权重。
4. 切换到动画模式，创建动画，添加位置、旋转、缩放等关键帧。
5. 播放检查姿态与蒙皮，调整时间和插值。
6. 保存工程，再按需要导出资源。导出前保留可继续编辑的原工程。

Create a rig, attach and bind images, adjust weights, animate keyframes, preview, save and export.

## 从源码编译 / Build from source

准备 / Requirements:

- Windows x64。
- Python 3.9 或更新版本，能够通过 `python` 命令调用。
- SCons；建议安装最新版本以匹配 Visual Studio 工具链。
- Visual Studio C++ 构建工具及 Windows SDK，安装“使用 C++ 的桌面开发”工作负载。

以下命令均在源码根目录执行。源码直接位于仓库根目录，包含完整构建输入。普通构建不需要重新生成图标；仅运行 `tools/generate_icon.py` 时需要 Pillow。

```powershell
$env:AGESKELETON_DATA_ROOT = [System.IO.Path]::GetFullPath("../AgeSkeletonData")
python -m pip install --upgrade scons
python tools/build.py --jobs 8
python tools/open_editor.py
```

`--jobs` 指定并行编译任务数，可按内存和 CPU 情况调整。构建成功后，程序生成在项目内的 `bin/`：`AgeSkeleton.exe` 和 `AgeSkeleton.console.exe`。编译中间文件位于 `bin/obj/`。编辑器内部缓存位于用户应用数据目录，不会写进作品目录；已有工作区不会被修改。

Executables and compiler output stay in the local `bin/` directory. Existing workspace projects are preserved.

查看待执行的构建步骤 / Inspect the build plan:

```powershell
python tools/build.py --dry-run
```

`bin/` 是实际目录，不创建快捷方式或目录联接。直接双击 EXE 新建空白工程；启动参数可以传入 `.ageskeleton` 文件。Windows 内部宿主和缓存位于 `%APPDATA%/AgeSkeleton/EditorHost`，旧的 `workspace.path` 不再决定默认作品。命令行 `--path` 仍用于打开旧 Godot 工作区。

The application keeps its host/cache in the user application-data directory. Open an `.ageskeleton` document directly; `--path` remains available for legacy Godot workspaces.

### 自定义数据目录 / Custom data directory

通过 `AGESKELETON_DATA_ROOT` 指定源码之外的数据目录。本文示例使用源码目录同级的 `AgeSkeletonData/`，也可以选择其他位置；构建、启动和回归须使用相同配置：

```powershell
$env:AGESKELETON_DATA_ROOT = [System.IO.Path]::GetFullPath("../AgeSkeletonData")
python tools/build.py --jobs 8
python tools/open_editor.py
```

此设置只选择工作区和案例等数据的位置，不会自动迁移已有工程。编译输出始终位于源码根目录的 `bin/`。

## 目录说明 / Layout

源码目录 / Source checkout:

```text
AgeSkeleton/
├─ modules/age_skeleton/       骨骼制作工具 / Skeleton authoring tools
├─ core/ editor/ scene/ ...    底层源码 / Engine source
├─ thirdparty/                 第三方代码及许可 / Third-party code
├─ tools/                      构建、启动和 AI 客户端 / CLI tools
├─ bin/                        程序及编译产物 / Executables and compiler output
├─ SConstruct                  SCons 构建入口 / Build entry
└─ README.md
```

外部数据目录 / External data directory:

```text
AgeSkeletonData/
├─ Project/
│  ├─ Workspace/               保留的旧工作区 / Legacy workspace
│  └─ AI-Authoring-Example/     本地 AI 案例 / Local AI example
├─ Build/
│  ├─ SDK/                     本地 SDK 工具 / Local SDK tooling
│  └─ Template/                平台导出模板 / Export templates
├─ Exports/                    按平台分类的导出 / Platform exports
└─ Reports/                    日志、截图和备份 / Reports and backups
```

SDK、平台模板、案例和旧产物属于本机外部数据，并非源码克隆后自动附带的内容。编译编辑器与导出各平台游戏包是两个步骤；游戏导出需要匹配的模板和平台工具链。

## 回归验证 / Validation

```powershell
python tools/validate.py --log "$env:AGESKELETON_DATA_ROOT/Reports/regression.log"
```

脚本在临时工程中运行内置回归；返回码为 `0` 且输出 `PASS` 表示通过。可用 `--binary` 指定另一个 EXE。自定义数据目录时，按需要修改 `--log` 路径。

The regression uses a temporary project. A passing headless test does not replace visual or device testing.

## AI 操作入口 / AI interface

为指定工程启用本地接口 / Enable the local API for a project:

```powershell
$project = Join-Path $env:AGESKELETON_DATA_ROOT "Project/Workspace"
& "./bin/AgeSkeleton.exe" --editor --path $project -- --ecs-ai-editor
```

启动后，在该工程的 `.godot` 目录中查找当前进程的 `session.json` 会话文件。可用以下命令列出会话路径；多个进程同时运行时，选择对应进程目录中的文件：

```powershell
Get-ChildItem -LiteralPath "$project/.godot" -Filter session.json -Recurse
```

客户端按 JSON 步骤执行操作：

```powershell
python tools/skeleton_ai.py "会话文件路径/session.json" "制作步骤.json"
python tools/skeleton_ai.py --help
```

先读取能力和当前状态，再提交修改；编辑器按版本检查并发修改，失败或超时后先核查状态，不要盲目重放。截图需要图形窗口，不能在 headless 模式下进行视觉验证。

## 许可证 / License

AgeSkeleton 原创编辑器、运行库和图标采用 [免费使用与限制商业化许可 1.0](LICENSE.txt)。可免费使用、自行修改和内部集成，无须仅因内部使用公开源码。**无论是否开源，均禁止对外商业发行衍生版本或提供集成编辑器／运行库的商业产品与服务，包括未修改的运行库。** 用户自己制作的图片、动画不自动受这份软件许可限制。

这是源码可用项目，不再按 MIT／OSI 开源许可宣传。已按 MIT 分发的旧版本既有授权不因本次修改撤销。

Godot 及其他第三方组件遵循各自许可证，见 [第三方说明](THIRD_PARTY_NOTICES.md) 和 [版权清单](COPYRIGHT.txt)。本项目不包含 Spine 编辑器或其商业运行时；第三方素材的使用权须由使用者自行取得。

AgeSkeleton 是独立项目，与 Esoteric Software 无隶属、合作或认可关系。Spine 名称仅用于描述格式兼容；导入、转换或重新打包不会改变原素材的许可条件。本项目不随附 Spine 官方示例素材。

AgeSkeleton is independent and is not affiliated with, sponsored by, or endorsed by Esoteric Software. Spine is referenced only to describe format compatibility. Importing or converting an asset does not change its license. Official Spine example artwork is not included.

Original AgeSkeleton software and icons use the Free Use and Restricted Commercialization License 1.0. Free use, private modification and internal integration are allowed without source publication. External commercial distribution and services, including commercial products integrating an unmodified runtime, are prohibited whether or not source code is disclosed. Independently created output is not covered merely because it was made with the editor. This is source-available, not MIT/OSI open source. Existing grants for previously distributed MIT revisions remain valid. Preserve all Godot and third-party notices.
