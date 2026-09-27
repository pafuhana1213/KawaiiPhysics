"""Generates the static meshes used by the sample exhibits with Geometry Script.

Builds SM_KPS_Pedestal (exhibit base with a grid deck), SM_KPS_BoardFrame
(sign board with frame, legs and feet) and SM_KPS_Placard_176/230/280 (subject
labels) into /Game/KawaiiPhysicsSample/Examples/Common/Meshes, plus the collider
visuals SM_KPS_3_2_Capsule and SM_KPS_3_3_TaperedCapsule into
/Game/KawaiiPhysicsSample/Examples/03_Collision/Meshes. Parts are lofted from
rounded outlines, get one material slot per surface role and one aligned box of
simple collision each. Existing assets are replaced in place, so the exhibit
Blueprints that reference them keep working.

Requires the GeometryScripting plugin (enabled for the editor in the uproject).
Run inside the editor (Output Log > Python, or `py "<path>"`):
    py "<project>/Tools/SampleMeshes/generate_sample_meshes.py"

Set ONLY_MESHES (a list of asset names) in the globals the script runs with to
rebuild just those meshes.

Dimensions are in cm and follow the exhibit layout: deck top at Z 18, board
face at X = 200, board Z 180..430. The collider visuals are modelled in limit
space (axis along +Z, origin at the limit centre) so they match the collision
shape exactly.
"""
import math
import unreal

MESH_DIR = '/Game/KawaiiPhysicsSample/Examples/Common/Meshes'
COLLISION_MESH_DIR = '/Game/KawaiiPhysicsSample/Examples/03_Collision/Meshes'
MAT = '/Game/KawaiiPhysicsSample/Examples/Common/Materials/'
MAT_COLLIDER = '/Game/KawaiiPhysicsSample/Other/Stage/Material/MI_Solid_Blue'

# ABP_3_2_Capsule CapsuleLimits[0]: Radius 16, Length 60 (distance between the end sphere centres)
CAPSULE_RADIUS = 16.0
CAPSULE_LENGTH = 60.0
# ABP_3_3_TaperedCapsule TaperedCapsuleLimits[0]: Radius0 22 at +Z, Radius1 6 at -Z, Length 70
TAPERED_RADIUS0 = 22.0
TAPERED_RADIUS1 = 6.0
TAPERED_LENGTH = 70.0
GS = unreal
ONLY = globals().get('ONLY_MESHES')


# ---------------------------------------------------------------- pure geometry
def rrect(cu, cv, hw, hh, r, seg):
    """Rounded rectangle, CCW in (u, v). 4*(seg+1) points (4 when seg == 0)."""
    if seg == 0:
        return [(cu + hw, cv + hh), (cu - hw, cv + hh), (cu - hw, cv - hh), (cu + hw, cv - hh)]
    pts = []
    corners = [(cu + hw - r, cv + hh - r, 0), (cu - hw + r, cv + hh - r, 90), (cu - hw + r, cv - hh + r, 180), (cu + hw - r, cv - hh + r, 270)]
    for (ox, oy, a0) in corners:
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            pts.append((ox + r * math.cos(a), oy + r * math.sin(a)))
    return pts


class Part:
    """Convex solid part; triangles are auto-oriented away from `center`."""

    def __init__(self, mesh, center):
        self.mesh = mesh
        self.center = center

    def add(self, mat, verts, tris):
        self.mesh.setdefault(mat, []).append((verts, tris, self.center))


def to3(axis, a, uv):
    u, v = uv
    if axis == 'x':
        return (a, u, v)
    if axis == 'z':
        return (u, v, a)
    raise ValueError(axis)


def loft(part, axis, ring_fn, profile, mat):
    """profile: list of (a, inset). ring_fn(inset) -> 2D points (same count)."""
    verts, tris = [], []
    n = None
    for a, inset in profile:
        ring = ring_fn(inset)
        n = len(ring)
        verts += [to3(axis, a, p) for p in ring]
    for j in range(len(profile) - 1):
        b0, b1 = j * n, (j + 1) * n
        for i in range(n):
            i2 = (i + 1) % n
            tris.append((b0 + i, b0 + i2, b1 + i2))
            tris.append((b0 + i, b1 + i2, b1 + i))
    part.add(mat, verts, tris)


def cap(part, axis, a, ring, mat):
    verts = [to3(axis, a, p) for p in ring]
    cu = sum(p[0] for p in ring) / len(ring)
    cv = sum(p[1] for p in ring) / len(ring)
    verts.append(to3(axis, a, (cu, cv)))
    c = len(verts) - 1
    n = len(ring)
    part.add(mat, verts, [(c, i, (i + 1) % n) for i in range(n)])


def annulus(part, axis, a, outer, inner, mat):
    n = len(outer)
    verts = [to3(axis, a, p) for p in outer] + [to3(axis, a, p) for p in inner]
    tris = []
    for i in range(n):
        i2 = (i + 1) % n
        tris.append((i, i2, n + i2))
        tris.append((i, n + i2, n + i))
    part.add(mat, verts, tris)


def arc(a_center, inset_center, radius, seg, start_deg, end_deg, a_sign=1.0):
    """Quarter-round profile points (a, inset)."""
    out = []
    for k in range(seg + 1):
        t = math.radians(start_deg + (end_deg - start_deg) * k / seg)
        out.append((a_center + a_sign * radius * math.cos(t), inset_center - radius * math.sin(t)))
    return out


def rounded_box(mesh, axis, cu, cv, hw, hh, r, seg, a0, a1, bevel, bseg, mat_side, mat_lo=None, mat_hi=None):
    """Box along `axis` from a0..a1 with rounded plan corners r and edge bevel at both ends."""
    center = to3(axis, (a0 + a1) / 2.0, (cu, cv))
    part = Part(mesh, center)
    ring = lambda inset: rrect(cu, cv, hw - inset, hh - inset, max(r - inset, 0.0), seg)
    prof = []
    # low end: from cap (a0, inset=bevel) round to (a0+bevel, 0)
    for k in range(bseg + 1):
        t = math.radians(90.0 * k / bseg)
        prof.append((a0 + bevel - bevel * math.cos(t), bevel - bevel * math.sin(t)))
    for k in range(bseg + 1):
        t = math.radians(90.0 * k / bseg)
        prof.append((a1 - bevel + bevel * math.sin(t), bevel - bevel * math.cos(t)))
    loft(part, axis, ring, prof, mat_side)
    cap(part, axis, a0, ring(bevel), mat_lo if mat_lo is not None else mat_side)
    cap(part, axis, a1, ring(bevel), mat_hi if mat_hi is not None else mat_side)
    return part, ring


# ---------------------------------------------------------------- mesh definitions
def build_pedestal():
    mesh = {}
    part = Part(mesh, (0.0, 0.0, 9.0))
    ring = lambda inset: rrect(0, 0, 225 - inset, 320 - inset, 0, 0)
    prof = []
    for k in range(5):  # R6, 4 segments: (z0, inset 6) -> (z6, inset 0)
        t = math.radians(90.0 * k / 4)
        prof.append((6 - 6 * math.cos(t), 6 - 6 * math.sin(t)))
    prof.append((18.0, 0.0))
    loft(part, 'z', ring, prof, 0)
    cap(part, 'z', 0.0, ring(6), 0)
    annulus(part, 'z', 18.0, ring(0), ring(8), 2)
    cap(part, 'z', 18.0, ring(8), 1)
    return mesh, ['Sides', 'DeckGrid', 'Rim'], [MAT + 'MI_KPS_Pedestal', MAT + 'MI_KPS_DeckGrid', MAT + 'MI_KPS_Rim'], [
        ((5.0 - 5.0, 0.0, 9.0), (450.0, 640.0, 18.0))]


def build_boardframe():
    mesh = {}
    # board: YZ outline, X 200..216, Z 180..430, R12 corners, R1 front/back bevel, 8 cm border
    part = Part(mesh, (208.0, 0.0, 305.0))
    seg = 6
    ring = lambda inset: rrect(0, 305, 320 - inset, 125 - inset, 12 - inset, seg)
    prof = []
    for k in range(4):
        t = math.radians(90.0 * k / 3)
        prof.append((201 - math.cos(t), 1 - math.sin(t)))
    for k in range(4):
        t = math.radians(90.0 * k / 3)
        prof.append((215 + math.sin(t), 1 - math.cos(t)))
    loft(part, 'x', ring, prof, 1)
    inner = rrect(0, 305, 312, 117, 4, seg)
    annulus(part, 'x', 200.0, ring(1), inner, 1)
    cap(part, 'x', 200.0, inner, 0)
    cap(part, 'x', 216.0, ring(1), 1)
    # legs 12x10x160 centred (213, +-270, 102); feet 28x44x4 centred (210, +-270, 20)
    for y in (-270.0, 270.0):
        rounded_box(mesh, 'z', 213.0, y, 6.0, 5.0, 2.0, 3, 22.0, 182.0, 1.0, 3, 1)
        rounded_box(mesh, 'z', 210.0, y, 14.0, 22.0, 2.0, 3, 18.0, 22.0, 1.0, 3, 1)
    boxes = [((208.0, 0.0, 305.0), (16.0, 640.0, 250.0))]
    for y in (-270.0, 270.0):
        boxes.append(((213.0, y, 102.0), (12.0, 10.0, 160.0)))
        boxes.append(((210.0, y, 20.0), (28.0, 44.0, 4.0)))
    return mesh, ['Face', 'Frame'], [MAT + 'MI_KPS_Face', MAT + 'MI_KPS_Frame'], boxes


def build_placard(w):
    mesh = {}
    rounded_box(mesh, 'x', 0.0, 0.0, w / 2.0, 13.0, 3.0, 6, -1.5, 1.5, 0.6, 3, 1, mat_lo=0, mat_hi=1)
    return mesh, ['Face', 'Frame'], [MAT + 'MI_KPS_Face', MAT + 'MI_KPS_Frame'], [((0.0, 0.0, 0.0), (3.0, w, 26.0))]


def build_tapered_capsule(r0, r1, length, radial=48, arc_seg=12):
    """Revolved tapered capsule along Z: hemisphere r0 centred at +length/2, straight frustum
    r0 -> r1, hemisphere r1 centred at -length/2 (the shape AdjustByTaperedCapsuleCollision
    resolves against; r0 == r1 gives a plain capsule)."""
    h = length / 2.0
    prof = []  # (z, radius) from the bottom pole to the top pole, poles excluded
    for k in range(1, arc_seg + 1):
        t = math.radians(-90.0 + 90.0 * k / arc_seg)
        prof.append((-h + r1 * math.sin(t), r1 * math.cos(t)))
    for k in range(arc_seg):
        t = math.radians(90.0 * k / arc_seg)
        prof.append((h + r0 * math.sin(t), r0 * math.cos(t)))
    verts = [(0.0, 0.0, -h - r1)]
    for z, r in prof:
        for i in range(radial):
            a = 2.0 * math.pi * i / radial
            verts.append((r * math.cos(a), r * math.sin(a), z))
    verts.append((0.0, 0.0, h + r0))
    top = len(verts) - 1
    tris = []
    for i in range(radial):
        tris.append((0, 1 + i, 1 + (i + 1) % radial))
    for j in range(len(prof) - 1):
        b0, b1 = 1 + j * radial, 1 + (j + 1) * radial
        for i in range(radial):
            i2 = (i + 1) % radial
            tris.append((b0 + i, b0 + i2, b1 + i2))
            tris.append((b0 + i, b1 + i2, b1 + i))
    last = 1 + (len(prof) - 1) * radial
    for i in range(radial):
        tris.append((top, last + i, last + (i + 1) % radial))
    mesh = {}
    Part(mesh, (0.0, 0.0, 0.0)).add(0, verts, tris)
    rmax = max(r0, r1)
    box = ((0.0, 0.0, (r0 - r1) / 2.0), (2.0 * rmax, 2.0 * rmax, length + r0 + r1))
    return mesh, ['Solid'], [MAT_COLLIDER], [box]


# ---------------------------------------------------------------- orientation + buffers
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def oriented(verts, tris, center):
    out, degenerate = [], 0
    for t in tris:
        a, b, c = verts[t[0]], verts[t[1]], verts[t[2]]
        n = cross(sub(b, a), sub(c, a))
        if dot(n, n) < 1e-12:
            degenerate += 1
            continue
        g = ((a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0, (a[2] + b[2] + c[2]) / 3.0)
        # UE treats clockwise (in this cross-product sense) as front-facing: keep n pointing inward
        out.append(t if dot(n, sub(g, center)) < 0 else (t[0], t[2], t[1]))
    return out, degenerate


def to_dynamic(mesh_parts):
    dm = unreal.DynamicMesh()
    degenerate = 0
    for mat, pieces in sorted(mesh_parts.items()):
        for verts, tris, center in pieces:
            tris2, d = oriented(verts, tris, center)
            degenerate += d
            buf = unreal.GeometryScriptSimpleMeshBuffers()
            buf.vertices = [unreal.Vector(*v) for v in verts]
            buf.triangles = [unreal.IntVector(*t) for t in tris2]
            # supply normals + UV0 so the DynamicMesh gets its attribute overlays (without them the
            # static mesh build asserts on an empty UV/normal array)
            nrm = []
            for v in verts:
                d = sub(v, center)
                l = math.sqrt(dot(d, d)) or 1.0
                nrm.append(unreal.Vector(d[0] / l, d[1] / l, d[2] / l))
            buf.normals = nrm
            buf.uv0 = [unreal.Vector2D(v[0] / 100.0 + v[2] / 100.0, v[1] / 100.0) for v in verts]
            GS.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, buf, material_id=mat)
    split = unreal.GeometryScriptSplitNormalsOptions()
    split.split_by_opening_angle = True
    split.opening_angle_deg = 40.0
    calc = unreal.GeometryScriptCalculateNormalsOptions()
    GS.GeometryScript_Normals.compute_split_normals(dm, split, calc)
    box = unreal.Transform(scale=unreal.Vector(100.0, 100.0, 100.0))
    GS.GeometryScript_UVs.set_mesh_u_vs_from_box_projection(dm, 0, box, unreal.GeometryScriptMeshSelection())
    q = GS.GeometryScript_MeshQueries
    nuv = q.get_num_uv_sets(dm)
    hasn = q.get_has_triangle_normals(dm) if hasattr(q, 'get_has_triangle_normals') else 'n/a'
    if nuv < 1 or hasn is False:
        raise RuntimeError('dynamic mesh has no UV/normal attributes (uv sets=%s normals=%s); refusing to build asset' % (nuv, hasn))
    return dm, degenerate


def collision_mesh(boxes):
    dm = unreal.DynamicMesh()
    for (c, s) in boxes:
        tr = unreal.Transform(location=unreal.Vector(c[0], c[1], c[2] - s[2] / 2.0))
        GS.GeometryScript_Primitives.append_box(dm, unreal.GeometryScriptPrimitiveOptions(), tr, s[0], s[1], s[2])
    return dm


def save_mesh(name, builder, mesh_dir=MESH_DIR):
    parts, slot_names, mats, boxes = builder()
    dm, degenerate = to_dynamic(parts)
    path = mesh_dir + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        sm = unreal.load_asset(path)
        opts = unreal.GeometryScriptCopyMeshToAssetOptions()
        opts.enable_recompute_normals = False
        opts.enable_recompute_tangents = True
        opts.replace_materials = False
        GS.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, opts, unreal.GeometryScriptMeshWriteLOD())
    else:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.enable_recompute_normals = False
        opts.enable_recompute_tangents = True
        opts.enable_nanite = False
        opts.enable_collision = True
        sm, outcome = GS.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts)
        print('  create outcome', outcome)
    sm.set_editor_property('static_materials', [
        unreal.StaticMaterial(material_interface=unreal.load_asset(m), material_slot_name=n) for n, m in zip(slot_names, mats)])
    # simple collision: one aligned box per part
    copts = unreal.GeometryScriptCollisionFromMeshOptions()
    copts.method = unreal.GeometryScriptCollisionGenerationMethod.ALIGNED_BOXES
    copts.emit_transaction = False
    GS.GeometryScript_Collision.set_static_mesh_collision_from_mesh(collision_mesh(boxes), sm, copts)
    body = sm.get_editor_property('body_setup')
    if body:
        body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
    sm.post_edit_change() if hasattr(sm, 'post_edit_change') else None
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    b = sm.get_bounding_box()
    tri = GS.GeometryScript_MeshQueries.get_num_triangle_i_ds(dm)
    ncol = GS.GeometryScript_Collision.get_simple_collision_shape_count(GS.GeometryScript_Collision.get_simple_collision_from_static_mesh(sm))
    print('%s tris=%d degenerate=%d bounds=(%s)..(%s) slots=%s collisionShapes=%s' % (
        name, tri, degenerate, b.min.to_tuple(), b.max.to_tuple(), [str(s.material_slot_name) for s in sm.static_materials], ncol))
    return sm


BUILDERS = [('SM_KPS_Pedestal', build_pedestal), ('SM_KPS_BoardFrame', build_boardframe)] + [
    ('SM_KPS_Placard_%d' % w, (lambda w=w: build_placard(float(w)))) for w in (176, 230, 280)]
COLLIDER_BUILDERS = [
    ('SM_KPS_3_2_Capsule', lambda: build_tapered_capsule(CAPSULE_RADIUS, CAPSULE_RADIUS, CAPSULE_LENGTH)),
    ('SM_KPS_3_3_TaperedCapsule', lambda: build_tapered_capsule(TAPERED_RADIUS0, TAPERED_RADIUS1, TAPERED_LENGTH))]
for name, fn in BUILDERS:
    if ONLY and name not in ONLY:
        continue
    save_mesh(name, fn)
for name, fn in COLLIDER_BUILDERS:
    if ONLY and name not in ONLY:
        continue
    save_mesh(name, fn, COLLISION_MESH_DIR)

# per-section shadow: no shadow from the display face of the board
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
bf = unreal.load_asset(MESH_DIR + '/SM_KPS_BoardFrame') if not ONLY or 'SM_KPS_BoardFrame' in ONLY else None
if bf:
    try:
        sms.enable_section_cast_shadow(bf, False, 0, 0)
        unreal.EditorAssetLibrary.save_asset(MESH_DIR + '/SM_KPS_BoardFrame', only_if_is_dirty=False)
        print('board face section cast shadow off')
    except Exception as e:
        print('section shadow failed', e)
