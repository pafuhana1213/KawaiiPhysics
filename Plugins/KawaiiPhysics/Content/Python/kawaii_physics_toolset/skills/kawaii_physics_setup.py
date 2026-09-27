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
  fixed strength).
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
  frames per second and the simulation behaves differently. Turn it off before measuring.
- Skeletal meshes outside the camera view stop refreshing bones with the default tick
  option, so sampled positions freeze. Keep the subject on screen (start PIE near it) or
  make the component always refresh bones while measuring.
- A bone's position is its pivot. To see a bone rotate, sample its child or the chain tip.
- When the actor itself moves, compare bone positions in component space; world positions
  mostly show the actor's own motion.
- In ComponentSpace, TeleportDistanceThreshold (default 300 cm) and
  TeleportRotationThreshold decide which component moves are ignored; check them before
  concluding that a jump "leaks" into the simulation.
- Python run through the console reports errors only in the output log.
"""


@agent_skill
class KawaiiPhysicsSetupSkill(unreal.AgentSkill):
    """Guidance for setting up, tuning and verifying KawaiiPhysics AnimGraph nodes through the editor tools.
    Apply when adding or editing KawaiiPhysics nodes, collisions, external forces or presets, or when checking their behavior in Play In Editor."""

    instructions = _INSTRUCTIONS
