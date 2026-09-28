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
- List parameters of these tools cannot be omitted over MCP; pass [] when not needed.
- Judge penetration and motion together. The penetration numbers alone pick bad settings:
  a skirt that runs away from the legs (for example inter-bone dummies that are not
  collision-only) reaches 0 penetration while it jitters, flips and flies up. Record the
  motion in the same run and compare it with the baseline every time.
- Tune in one loop, one change per trial, and never keep a trial that makes penetration or
  motion worse. Example with the sample project's skirt (actor Ex5_5_B, 16 columns,
  animation A_CheckSkirt: one loop is 18.3 s and the character stands still for the first
  1.5 s; the collision-only baseline is Ex5_5_A):
  1. Find the node: describe_kawaii_physics_bones_on_actor(actor_label="Ex5_5_B",
     filter_tag_names=[], summary_only=True) gives the bone, dummy and constraint counts
     (the load); without summary_only it lists every bone.
  2. Save the current settings: describe_graph_node_settings(anim_blueprint, []) and keep
     the JSON as the snapshot of the current best.
  3. Check the input pose right after PIE starts, while the character stands still:
     check_collision_clearance_on_actor(actor_label="Ex5_5_B", filter_tag_names=[],
     position_source="both"). Contacts in "pose" mean the leg collisions or bone radii
     already overlap before any physics runs; contacts only in "simulated" are the push-out
     result.
  4. Measure over at least one loop of the motion, recording the bones:
     start_collision_penetration_sampler(actor_label="Ex5_5_B",
       ring_root_bones=["skirt_01_01_l", "skirt_02_01_l", "skirt_03_01_l", "skirt_04_01_l",
                        "skirt_05_01_l", "skirt_06_01_l", "skirt_07_01_l", "skirt_08_01_l",
                        "skirt_08_01_r", "skirt_07_01_r", "skirt_06_01_r", "skirt_05_01_r",
                        "skirt_04_01_r", "skirt_03_01_r", "skirt_02_01_r", "skirt_01_01_r"],
       frames=700, filter_tag_names=[], min_depth=1, threshold=1.0, warmup_frames=40,
       record_path="<absolute path>.jsonl",
       record_extra_bones=["calf_l", "calf_r", "thigh_l", "thigh_r"])
     The column roots go around the waist in order. While sampling, game time advances at
     fixed_frame_rate (default 30) so the sampling cost does not change the physics step;
     choose frames as loop length x fixed_frame_rate or more (one 18.3 s loop at 30 is 549
     frames; 700 adds margin). The editor itself runs slower in real time while sampling;
     do not take screenshots meanwhile (they stall the mesh update).
     Poll get_collision_penetration_sampler_result() while status is "running"; it is
     finished at "ok", "penetration", "nothing_checked" or "error" ("stopped" means
     stop_collision_penetration_sampler ended it early; do not compare it). Compare per
     category (vertical = along a column, horizontal = between neighbouring columns,
     other = constraints outside the ring) mainly samples_over_threshold, mean_all and max;
     mean_positive alone misleads when positive_count is small. top_events gives the worst
     moments with their PIE time, to find the scene in the animation. max is decided by a few
     frames and varies between identical runs; measure the baseline twice to know the noise.
  5. analyze_motion_recording(record_path, baseline_path) scores the motion: deviation from
     the pose (much lower = stiffer), lift (p50 is stable, max varies a lot between runs),
     jitter, rotation spikes and flips (> 90 degrees in one frame), collapse (vertical
     segments shorter than 70% of rest), and stretch between ring neighbours measured
     against the rest pose. flags (stiffer, jittery, pops, flips, collapses, lifts,
     stretches) compare with the baseline using a ratio and an absolute floor; they are hints,
     so look at the recording before acting. status "stale" means the mesh stopped updating
     for more than 10% of the frames: measure again.
  6. Change one thing, compile, restart PIE (stop PIE before saving: a save during PIE fails
     silently) and measure again with the same motion, frames, warm-up, fixed_frame_rate and
     ring_root_bones. Keep the change only when penetration improves and the motion does not
     get worse beyond the noise; otherwise restore the snapshot with
     apply_graph_node_settings(anim_blueprint, snapshot, 0) and check with
     diff_graph_node_settings that only the next change differs. Leftovers from a reverted
     trial silently spoil the following measurements.
- Bones pinned to the input pose (skip_simulate, such as the column roots) are not checked:
  the collision step skips them, so overlap there is fixed by moving the collision or the
  pose, not by physics settings.
- Which setting to change (measured on the sample skirt, collision-only baseline = 1):
  - Contacts already in the pose: move or shrink the leg capsules, or lower the radius of
    the rows that overlap.
  - A leg slips between columns ("horizontal"): ring BoneConstraints plus
    BoneConstraintSubdivisionCount 2 cut the samples over 1 cm to about a tenth. More than 2
    only lowers "vertical". A larger radius removes bridge dummies (they are not added where
    the endpoint spheres already overlap) and made it worse.
  - BoneConstraintSubdivisionFeedbackScale above 1 lowers horizontal penetration but brings
    back flips in fast kicks; 1.0 was the best balance.
  - BoneSubdivisionCount 1 with bBoneSubdivisionCollisionOnly lowers "vertical" and the
    tip bounce in kicks. 2 or DensifyByRadius added penetration and lift. Keep
    bBoneSubdivisionCollisionOnly on: simulated inter-bone dummies made the skirt jitter
    and fly away from the legs.
  - SyncBone (from the calf) raises the hem over the knee while walking (lift p50 about
    1.5x the collision-only skirt) in exchange for fewer contacts; the combined setup keeps
    that lift. GlobalScale around 0.65 avoids the hem riding up in jumps.
  - Stiffness trades the two: 0.1 gave the calmest hem but pulled it back into the legs
    (horizontal about 1.8x); 0.02 lowered penetration but left flips after jumps. Settings
    that push harder raise flips; decide with both numbers.
  - The skirt looks stiff rather than penetrating: adjust Damping / Stiffness, not the
    collision.
- build_ring_bone_constraints(skeletal_mesh, ring_root_bones, 1, -1, True) pairs the same
  depth of neighbouring columns. Set its import_text on BoneConstraints, or pass its result
  to set_bone_constraints_data_asset_pairs (try dry_run first; a data asset is shared by
  every node that references it). Constraints keep neighbouring columns at their rest
  distance. BoneConstraintSubdivisionCount only adds collision dummies along constraints:
  it fills collision gaps between columns but does not hold them together. Keep
  bAutoAddChildDummyBoneConstraint on with BoneSubdivision; it links the tips too.
- set_graph_node_radius_by_depth(handle, [2, 2, 3, 4, 4.5], skeletal_mesh) writes Radius and
  RadiusCurveData for one radius per row (row 0 = column root). Check the per-bone error it
  returns; columns of different lengths are averaged and listed in warnings. It refuses a
  RadiusCurve that uses an external curve.
- SyncBone follows only the position change of its bone. Sync from the knee (calf), not
  the hip joint, which barely moves. For jumps set ApplyDirectionZ to None so the skirt does
  not follow the vertical motion.
- The sampler value is a proxy from bone placement (how deep a collision reaches into the
  segments between bones), not the cloth mesh's own penetration. 1.0 cm is a guide, not a
  pass criterion; judge the view too. For the final comparison look at the same moments of
  the baseline and the candidates side by side, captured at the fixed frame rate (a
  SceneCapture2D in the PIE world writes each frame synchronously; high resolution
  screenshots stall PIE time and show an editor notification per shot). Measure the scene
  times in the capture run itself; they drifted about 0.4 s from the sampler's times. Among
  candidates with similar numbers, rotation spikes matched the visible hem disorder better
  than the horizontal counts. A front camera cannot judge back kicks.
- Ring constraints keep the hem from flaring in spins (it rides up instead); changing their
  Compliance (Tendon, Rubber) did not change that. Settings multipliers applied during a run
  change the radii and therefore the numbers. Convex simple world shapes and the world collision sweep are not checked (the
  results list them in reasons).

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
