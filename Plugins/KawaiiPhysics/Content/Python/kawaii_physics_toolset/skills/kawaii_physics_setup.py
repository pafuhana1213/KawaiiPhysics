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
