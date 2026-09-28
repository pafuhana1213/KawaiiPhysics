import unreal

from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """\
CHOOSING BONES
- A node simulates the descendants of RootBone; RootBone itself stays driven by the
  input pose. A bone without children (bangs, skirt panels parented straight to the
  pelvis) only rotates when DummyBoneLength is greater than 0.
- When the natural root also owns unrelated children (Pelvis -> spine and legs, Head ->
  eyes and brows), either exclude them (ExcludeBones removes the bone and its children)
  or pick the chain starts and add siblings through AdditionalRootBones.
- Use one node per body part and give each a KawaiiPhysicsTag; runtime functions,
  presets and audits filter by tag.
- A test AnimBlueprint for a character needs an input pose (a sequence player) before
  the KawaiiPhysics nodes; props such as a hanging chain work from the reference pose.
- Compile and read the compiler messages; an unknown bone is reported there, not as an
  error from the property setter.

PROPERTY VALUES
- String values use Unreal ImportText. A partial struct such as "(Damping=0.3)" only
  changes the listed fields and keeps the rest.
- Collision OffsetLocation / OffsetRotation are in the DrivingBone's own local space,
  not component space. Convert a component-space placement with the bone's reference
  pose (offset = inverse(bone ref pose) * target). A chain root with a -90 pitch turns
  "X = 10" into a 10 cm move along the chain. A planar limit with an empty DrivingBone
  is placed directly in component space.
- Capsules extend along their local Z axis; a planar limit's normal is its local Z.
  Planar limits only push bones that cross the plane during a step or come within the
  bone radius, so a bone that starts behind the plane stays there; place planes on the
  side the bones approach from.
- ExternalForces are instanced structs; they cannot be edited through the plain string
  property path. Force strength comes from RandomForceScaleRange (min = max for a
  fixed strength). Edit them with get_graph_node_external_forces /
  set_graph_node_external_forces. Forces added at runtime from an actor Blueprint use
  AddExternalForcesOnComponent / RemoveExternalForcesOnComponent; the ...ToComponent /
  ...FromComponent names are deprecated.
- ExternalForces' Force Space: BoneSpace reads ForceDir in each bone's own local axes (there is
  no base bone setting), so on a chain whose bones all share the component axes it looks the
  same as ComponentSpace; use a character whose bones are rotated to see the difference.
- bUseLegacyGravity: true adds 0.5 * Gravity * dt^2 to the position (old behavior); false
  (default) adds Gravity * dt to the velocity like AnimDynamics. With the default Stiffness
  0.05 the difference is only a few centimeters; it grows as Stiffness goes down.
- bNeedWarmUp / WarmUpFrames only show a difference when the rest shape differs from the
  reference pose (for example a chain that droops under gravity). bUseWarmUpWhenResetDynamics
  also warms up after ResetDynamics; trigger a reset while verifying with reset_dynamics_on_actor.
- describe_graph_node_settings returns the settings specific to a KawaiiPhysics node
  (FAnimNode_KawaiiPhysics: physics settings, curves, collision limits, external forces, bone
  constraints, sync bones, gravity, wind) as JSON; use it before changing a node you did not
  create. Base AnimNode properties such as Alpha, AlphaInputType and LODThreshold are not
  included; read the runtime Alpha with describe_kawaii_physics_runtime_on_actor.
- World collision sweeps with the SkeletalMeshComponent's own collision settings. A mesh
  set to NoCollision never hits anything; use a query profile or override the collision
  params on the node. Simple world collision is a separate, cheaper feature.
- Simple world collision only gathers components whose object type is in
  SimpleWorldCollisionObjectTypes (empty = WorldStatic and WorldDynamic). Characters usually
  use a Pawn object type, so another character is ignored until its type is added or its
  profile changes; the skeletal-mesh mode (None / BoundingBox / PhysicsAsset) then decides
  the shape. Meshes with complex-as-simple collision and landscapes are not gathered as
  shapes; the ground comes from movement data or a downward trace instead.
- Shared collision needs bSharedCollisionSource on the publishing node, bUseSharedCollision
  on the consumer and the same SharedCollisionGroupTag on both.

WIND
- Gust notifies, gust functions and wind presets act on a ProceduralWind entry in the node's
  ExternalForces; a node without one ignores them. Add ProceduralWind with zero forces when
  only gusts are wanted.
- A Kawaii Physics Shared Publisher node hands one wind setup, its phase and gusts (and
  simple world collision gathering) to every node in the same actor family that reads it:
  ProceduralWind WindSource and SimpleWorldCollisionSource set to Shared or Auto with the same
  tag. Put the publisher where the AnimGraph always updates (the trunk before Output Pose or a
  post-process AnimBlueprint), never inside a blend branch or state that can drop to zero
  weight.
- Settings multipliers (notify, notify state, Blueprint functions and the Sequencer track)
  multiply the authored settings and each other; the Sequencer track belongs on a
  SkeletalMeshComponent binding. A root-level track affects every skeletal mesh in the world.
- WorldDampingLocation / WorldDampingRotation default to 0.8, so only 20% of the component's
  own movement reaches the bones. Motion driven by moving the actor looks weak until these
  are lowered; motion from the animation is not affected.

VERIFYING IN PLAY IN EDITOR
- With "Use Less CPU when in Background" enabled, an unfocused editor runs PIE at a few
  frames per second and the simulation behaves differently. Turn it off before measuring;
  from Python, call set_background_cpu_throttle(False) when the editor is not focused and
  restore the previous value afterward.
- Skeletal meshes outside the camera view stop refreshing bones with the default tick
  option, so sampled positions freeze. Keep the subject on screen (start PIE near it) or
  make the component always refresh bones while measuring.
- A bone's position is its pivot. To see a bone rotate, sample its child or the chain tip.
- When the actor itself moves, compare bone positions in component space; world positions
  mostly show the actor's own motion.
- Besides comparing screenshots, measure motion with start_bone_sampler / get_bone_sampler_result.
- In ComponentSpace, TeleportDistanceThreshold (default 300 cm) and
  TeleportRotationThreshold decide which component moves are ignored; check them before
  concluding that a jump "leaks" into the simulation. TeleportRotationThreshold is compared
  against a single frame's rotation, so whether a turn counts as a teleport depends on the
  frame rate; 0 disables it.
- Python run through the console reports errors only in the output log.

SKIRTS: SET UP, CHECK AND TUNE
- A skirt is several bone columns hanging from the hips. Put every column root (not the
  pelvis) in RootBone / AdditionalRootBones so the waist row stays on the pose, and give
  the chains a DummyBoneLength so the last row can swing.
- The check tools read what the running node actually uses (bones, radii after
  multipliers, collisions, merged constraints) in component space, in cm. Every result has
  a status. "not_found", "no_nodes", "not_evaluated" and "nothing_checked" mean nothing was
  measured (an empty result is not a pass); "not_evaluated" means PIE is not running or the
  mesh has not updated (off screen, background throttle). "ok" means checked with no
  finding; "contact" (clearance) and "penetration" (sampler) mean checked with findings.
- Tune in one loop, changing one kind of setting per round. Example with the sample
  project's skirt (actor Ex5_1_B, 16 columns, animation A_CheckSkirt: one loop is 18.3 s and
  the character stands still for the first 1.5 s):
  1. Find the node: describe_kawaii_physics_bones_on_actor(actor_label="Ex5_1_B").
  2. Check the input pose right after PIE starts, while the character stands still:
     check_collision_clearance_on_actor(actor_label="Ex5_1_B", position_source="both").
     Contacts in "pose" mean the leg collisions or bone radii already overlap before any
     physics runs; contacts only in "simulated" are the push-out result.
  3. Measure a baseline over at least one loop of the motion:
     start_collision_penetration_sampler(actor_label="Ex5_1_B",
       ring_root_bones=["skirt_01_01_l", "skirt_02_01_l", "skirt_03_01_l", "skirt_04_01_l",
                        "skirt_05_01_l", "skirt_06_01_l", "skirt_07_01_l", "skirt_08_01_l",
                        "skirt_08_01_r", "skirt_07_01_r", "skirt_06_01_r", "skirt_05_01_r",
                        "skirt_04_01_r", "skirt_03_01_r", "skirt_02_01_r", "skirt_01_01_r"],
       frames=700, min_depth=1, threshold=1.0, warmup_frames=40)
     The column roots go around the waist in order. While sampling, game time advances at
     fixed_frame_rate (default 30) so the sampling cost does not change the physics step;
     choose frames as loop length x fixed_frame_rate or more (one 18.3 s loop at 30 is 549
     frames; 700 adds margin). The editor itself runs slower in real time while sampling.
     Poll get_collision_penetration_sampler_result() while status is "running"; it is
     finished at "ok", "penetration", "nothing_checked" or "error" ("stopped" means
     stop_collision_penetration_sampler ended it early; do not compare it). Compare mainly
     max and mean_positive per category (vertical = along a column, horizontal = between
     neighbouring columns, other = constraints outside the ring, usually empty) and look at
     "worst"; samples_over_threshold depends on how many frames were run. Many
     unchanged_frames mean the mesh stopped updating (off screen or throttled).
  4. Change one thing: radius by depth, constraints, leg capsules or SyncBone.
  5. Compile the AnimBlueprint, restart PIE and measure again with the same motion, start
     phase, frames, warm-up, fixed_frame_rate and ring_root_bones (the segment set must
     match). Compare the numbers and the view side by side.
- Bones pinned to the input pose (skip_simulate, such as the column roots) are not checked:
  the collision step skips them, so overlap there is fixed by moving the collision or the
  pose, not by physics settings.
- Which setting to change:
  - Contacts already in the pose: move or shrink the leg capsules, or lower the radius of
    the rows that overlap.
  - Only "horizontal" is high (a leg slips between columns): raise the lower rows' radius
    with set_graph_node_radius_by_depth and add ring constraints.
  - The skirt looks stiff rather than penetrating: adjust Damping / Stiffness, not the
    collision.
- build_ring_bone_constraints(skeletal_mesh, ring_root_bones, 1, -1, True) pairs the same
  depth of neighbouring columns. Set its import_text on BoneConstraints, or pass its result
  to set_bone_constraints_data_asset_pairs (try dry_run first; a data asset is shared by
  every node that references it). Constraints keep neighbouring columns at their rest
  distance. BoneConstraintSubdivisionCount only adds collision dummies along constraints:
  it fills collision gaps between columns but does not hold them together.
- set_graph_node_radius_by_depth(handle, [2, 2, 3, 4, 4.5], skeletal_mesh) writes Radius and
  RadiusCurveData for one radius per row (row 0 = column root). Check the per-bone error it
  returns; columns of different lengths are averaged and listed in warnings. It refuses a
  RadiusCurve that uses an external curve.
- SyncBone follows only the position change of its bone. Sync from the knee (calf), not
  the hip joint, which barely moves. For jumps set ApplyDirectionZ to None so the skirt does
  not follow the vertical motion.
- The sampler value is a proxy from bone placement (how deep a collision reaches into the
  segments between bones), not the cloth mesh's own penetration. 1.0 cm is a guide, not a
  pass criterion; judge the view too. Settings multipliers applied during a run change the
  radii and therefore the numbers. Convex simple world shapes and the world collision sweep
  are not checked (the results list them in reasons).

SEQUENCER AND POST PROCESS
- In UE 5.8 an animation section plays as a DefaultSlot montage when the actor's
  AnimBlueprint has a slot, so KawaiiPhysics in that AnimBlueprint keeps running; it stops
  only when the section uses Force Custom Mode or the AnimBlueprint cannot be used, because
  Sequencer then swaps in its own AnimSequencerInstance.
- To keep physics under any playback method, put KawaiiPhysics in a post process
  AnimBlueprint (create_post_process_anim_blueprint, then set_post_process_anim_blueprint on
  the skeletal mesh). Check which instance is actually running with
  describe_kawaii_physics_runtime_on_actor.

ANIMNODE FUNCTIONS
- bind_graph_node_function creates a thread-safe function with the required signature (const
  Context and Node references) and binds it to On Initial Update, On Become Relevant or On
  Update. In the body convert the Node pin with ConvertToKawaiiPhysics, then call the
  KawaiiPhysicsLibrary setters (SetPhysicsSettings and so on), and compile with
  compile_anim_blueprint. Never delete and recreate a function graph that a node is bound to:
  doing that from editor Python crashed the editor.
- To drive Alpha from an animation curve, connect Get Curve Value to the node's Alpha pin.
"""


@agent_skill
class KawaiiPhysicsSetupSkill(unreal.AgentSkill):
    """Guidance for setting up, tuning and verifying KawaiiPhysics AnimGraph nodes through the editor tools.
    Apply when adding or editing KawaiiPhysics nodes, collisions, external forces or presets, or when checking their behavior in Play In Editor."""

    instructions = _INSTRUCTIONS
