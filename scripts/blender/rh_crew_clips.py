"""rh_crew_clips - the ONE crew clip set, authored on MPFB's game_engine rig.

Imported by rh_crew_build.py; every crew member gets these same clips, keyed by
the same code on the same 53-bone skeleton, so the motion is identical across
the roster by construction (the old per-walker 14-bone rigs placed joints at
fixed height fractions, so one clip moved every body differently-wrong).

Clip contract (art bible section 5; names and lengths match what
scripts/gpu/lane/rig_colonist.py shipped, 24 fps):
  Walk 1-25 (loop)   Idle 1-49   WorkBench 1-25   WorkLab 1-49
  Dig 1-33  Haul 1-33  Charge 1-49  Plant 1-33  Repair 1-33  Operate 1-33
The game plays Idle / Walk / WorkBench / WorkLab for crew
(RHCrewVisualizerSubsystem.cpp ~695); the rest complete the contract.

HOW ROTATIONS ARE SPECIFIED. Every key is a rotation about a WORLD axis at the
rest pose, converted into the bone's own rest frame, so it does not depend on
MPFB's bone rolls. MPFB characters face -Y with their left at +X, so:
  fwd(d)   leg / arm swings forward (toward -Y)       = about +X by -d
  knee(d)  knee bends (foot goes back)               = about +X by +d
  elbow(d) elbow bends (hand comes forward and up)   = about +X by -d
  lean(d)  spine / neck / head pitches forward       = about +X by +d
  toes(d)  ankle lifts the toes (dorsiflex)          = about +X by -d
  turn(d)  yaw to the character's left               = about +Z by +d
  tilt(d)  lean toward the character's left          = about +Y by +d
Sagittal rotations about the same world axis compose down a chain, which is
what makes the crouch maths below exact.

CROUCH WITHOUT IK: hip flex k, knee 2k, ankle k keeps the foot flat and directly
under the hip, and drops the pelvis by 2L(1 - cos k) for leg segment length L -
so Dig and Plant crouch with the feet planted, no sliding through the floor.

WALK STRIDE: the game speed-matches the Walk clip with rh.CrewStrideCm (104.4 cm
per cycle at the 1.8 m figure height the visualizer normalises to). The thigh
swing is tuned per character so the authored stride, scaled to 1.8 m, lands on
that value (stride_cm() measures it).
"""
import math

import bpy
from mathutils import Quaternion, Vector

AX = Vector((1.0, 0.0, 0.0))
AY = Vector((0.0, 1.0, 0.0))
AZ = Vector((0.0, 0.0, 1.0))
STRIDE_CM_AT_1_8 = 104.4


def q(axis, deg):
    return Quaternion(axis, math.radians(deg))


def fwd(d):
    return q(AX, -d)


def knee(d):
    return q(AX, d)


def elbow(d):
    return q(AX, -d)


def lean(d):
    return q(AX, d)


def toes(d):
    return q(AX, -d)


def turn(d):
    return q(AZ, d)


def tilt(d):
    return q(AY, d)


def inward(side, d):
    """Turn a forearm toward the body midline. MPFB's A-pose forearms carry an
    outward splay, so hands at a bench sat wider than the elbows (measured
    2026-10-13: hands +-0.34 m vs elbows +-0.20 m)."""
    return turn(-d if side == "l" else d)


class Rig:
    """Keying helper bound to one MPFB game_engine armature object."""

    SIDES = ("l", "r")
    FINGERS = ("index_", "middle_", "ring_", "pinky_", "thumb_")

    def __init__(self, rig):
        self.rig = rig
        self.pb = rig.pose.bones
        for pb in self.pb:
            pb.rotation_mode = "QUATERNION"
        b = rig.data.bones
        self.L = ((b["thigh_l"].length + b["calf_l"].length) / 2.0)
        self.base = self._arm_base()

    # rest-frame conversion: world-axis rotation -> bone-local quaternion
    def local(self, bone, qw):
        r = self.rig.data.bones[bone].matrix_local.to_quaternion()
        return r.inverted() @ qw @ r

    def key_rot(self, bone, frame, qw):
        pb = self.pb[bone]
        pb.rotation_quaternion = self.local(bone, qw)
        pb.keyframe_insert("rotation_quaternion", frame=frame)

    def key_loc(self, bone, frame, world_vec):
        pb = self.pb[bone]
        r = self.rig.data.bones[bone].matrix_local.to_quaternion()
        pb.location = r.inverted() @ Vector(world_vec)
        pb.keyframe_insert("location", frame=frame)

    def _arm_base(self):
        """Arms from MPFB's ~45 deg A-pose down to a relaxed hang (~12 deg out),
        and the rest-pose elbow (~43 deg) opened to a relaxed ~18 deg."""
        out = {}
        for s in self.SIDES:
            sign = 1.0 if s == "l" else -1.0
            bu = self.rig.data.bones["upperarm_" + s]
            rest = (bu.tail_local - bu.head_local).normalized()
            target = Vector((sign * 0.21, 0.02, -0.977)).normalized()
            out["upperarm_" + s] = rest.rotation_difference(target)
            out["lowerarm_" + s] = elbow(-25.0)
            out["clavicle_" + s] = Quaternion()
        return out

    # a pose is {bone: world quaternion}; arms get the relaxed base underneath
    def pose(self, frame, rots, pelvis_drop=0.0, pelvis_shift=(0.0, 0.0)):
        keyed = set()
        for bone, qw in rots.items():
            if bone.startswith("_"):        # settings, not bones (_grip_l/_grip_r)
                continue
            base = self.base.get(bone)
            self.key_rot(bone, frame, qw @ base if base is not None else qw)
            keyed.add(bone)
        for bone, base in self.base.items():
            if bone not in keyed:
                self.key_rot(bone, frame, base)
                keyed.add(bone)
        # Key EVERY other bone at rest too: a bone a clip leaves unkeyed keeps
        # whatever the previous clip left in Blender (and the exporter is then
        # free to write it), so each clip owns the whole pose.
        for pb in self.pb:
            n = pb.name
            if n in keyed or n.startswith(self.FINGERS) or n == "Root":
                continue
            self.key_rot(n, frame, Quaternion())
        self.key_loc("pelvis", frame, (pelvis_shift[0], pelvis_shift[1], -pelvis_drop))
        self.hands(frame, rots.get("_grip_l", 18.0), rots.get("_grip_r", 18.0))

    def hands(self, frame, grip_l, grip_r):
        """Curl every finger about its own local X (MPFB's finger rolls put the
        flexion axis there). grip 18 = relaxed, 70+ = a fist on a tool."""
        for s, g in (("l", grip_l), ("r", grip_r)):
            for f in ("index", "middle", "ring", "pinky"):
                for seg, w in (("01", 0.8), ("02", 1.0), ("03", 0.7)):
                    pb = self.pb["%s_%s_%s" % (f, seg, s)]
                    pb.rotation_quaternion = Quaternion(AX, math.radians(g * w))
                    pb.keyframe_insert("rotation_quaternion", frame=frame)
            pb = self.pb["thumb_01_%s" % s]
            pb.rotation_quaternion = Quaternion()
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            for seg in ("02", "03"):
                pb = self.pb["thumb_%s_%s" % (seg, s)]
                pb.rotation_quaternion = Quaternion(AX, math.radians(g * 0.4))
                pb.keyframe_insert("rotation_quaternion", frame=frame)

    def crouch(self, k):
        """Rotations + pelvis drop for a feet-planted crouch at hip flex k."""
        rots = {}
        for s in self.SIDES:
            rots["thigh_" + s] = fwd(k)
            rots["calf_" + s] = knee(2 * k)
            rots["foot_" + s] = toes(k)
        return rots, 2.0 * self.L * (1.0 - math.cos(math.radians(k)))


def _action(rig, name, start, end):
    act = bpy.data.actions.new(name)
    rig.animation_data_create()
    rig.animation_data.action = act
    act.use_fake_user = True
    act.use_frame_range = True
    act.frame_start, act.frame_end = start, end
    return act


def walk(R, swing):
    """24-frame loop: left heel strike at 1, right at 13."""
    act = _action(R.rig, "Walk", 1, 25)
    # per-leg phase tables (left leg; right is offset by 12 frames)
    hip = {0: swing, 3: swing * 0.75, 6: swing * 0.2, 9: -swing * 0.35, 12: -swing * 0.7,
           15: -swing * 0.35, 18: swing * 0.55, 21: swing * 1.05}
    kn = {0: 4, 3: 16, 6: 9, 9: 4, 12: 8, 15: 34, 18: 58, 21: 26}
    ank = {0: 10, 3: -3, 6: 2, 9: -4, 12: -14, 15: -22, 18: 8, 21: 12}
    for f in range(0, 24, 3):
        rots = {}
        for s, off in (("l", 0), ("r", 12)):
            p = (f + off) % 24
            rots["thigh_" + s] = fwd(hip[p])
            rots["calf_" + s] = knee(kn[p])
            rots["foot_" + s] = toes(ank[p])
        # arms swing opposite to the legs (left arm back while the left leg
        # strikes forward), elbows bend more on the forward swing
        a = 0.62 * swing * math.cos(math.radians(f / 24.0 * 360.0))
        rots["upperarm_l"] = fwd(-a)
        rots["upperarm_r"] = fwd(a)
        rots["lowerarm_l"] = elbow(12 + max(0.0, -a) * 0.6)
        rots["lowerarm_r"] = elbow(12 + max(0.0, a) * 0.6)
        # pelvis: low at contact (0, 12), high at passing (6, 18); hips and
        # shoulders counter-rotate; the head stays level
        ph = math.cos(math.radians(f / 24.0 * 720.0))
        side = math.sin(math.radians(f / 24.0 * 360.0))
        rots["pelvis"] = turn(4.0 * math.cos(math.radians(f / 24.0 * 360.0))) @ tilt(-2.5 * side)
        rots["spine_02"] = turn(-3.0 * math.cos(math.radians(f / 24.0 * 360.0))) @ lean(3.0)
        rots["spine_03"] = turn(-2.0 * math.cos(math.radians(f / 24.0 * 360.0)))
        rots["head"] = lean(-2.0)
        R.pose(1 + f, rots, pelvis_drop=0.012 + 0.012 * ph, pelvis_shift=(0.018 * side, 0.0))
    _close_loop(R, act, 1, 25)
    return act


def _close_loop(R, act, first, last):
    """Copy the first frame's keys to the last frame so the loop is seamless."""
    for fc in _fcurves(act):
        v = fc.evaluate(first)
        fc.keyframe_points.insert(last, v, options={"FAST"})
    for fc in _fcurves(act):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
        fc.update()


def _fcurves(act):
    """Blender 4.4+ layered actions keep curves in channelbags; older ones on
    action.fcurves. Support both."""
    if hasattr(act, "layers") and len(act.layers):
        out = []
        for layer in act.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    out.extend(bag.fcurves)
        if out:
            return out
    return list(getattr(act, "fcurves", []))


def idle(R):
    act = _action(R.rig, "Idle", 1, 49)
    for f, t in ((1, 0.0), (13, 0.5), (25, 1.0), (37, 0.5), (49, 0.0)):
        breathe = math.sin(math.radians(t * 180.0))
        R.pose(f, {
            "spine_03": lean(-1.2 * breathe),
            "neck_01": lean(1.0 * breathe),
            "head": turn(3.0 * math.sin(math.radians(t * 360.0))),
            "clavicle_l": q(AY, -1.5 * breathe), "clavicle_r": q(AY, 1.5 * breathe),
            "pelvis": tilt(1.2 * math.sin(math.radians(t * 360.0))),
            "upperarm_l": fwd(1.5 * breathe), "upperarm_r": fwd(1.5 * breathe),
        }, pelvis_drop=0.004 * breathe, pelvis_shift=(0.008 * math.sin(math.radians(t * 360.0)), 0.0))
    return act


def workbench(R):
    """Hammering at a bench: lean in, left hand braced, right arm strikes."""
    act = _action(R.rig, "WorkBench", 1, 25)
    keys = {1: (55, 70), 7: (95, 105), 13: (38, 40), 19: (50, 62), 25: (55, 70)}
    for f, (sh, el) in keys.items():
        R.pose(f, {
            "spine_02": lean(10.0), "spine_03": lean(6.0), "neck_01": lean(6.0), "head": lean(8.0),
            "upperarm_l": fwd(42.0) @ q(AZ, -8.0), "lowerarm_l": inward("l", 15) @ elbow(55.0),
            "upperarm_r": fwd(sh) @ q(AZ, 8.0), "lowerarm_r": elbow(el),
            "_grip_l": 35.0, "_grip_r": 75.0,
        }, pelvis_drop=0.01)
    return act


def worklab(R):
    """Careful benchwork: both hands up, alternating pours, glance between."""
    act = _action(R.rig, "WorkLab", 1, 49)
    for f, t in ((1, 0.0), (13, 0.25), (25, 0.5), (37, 0.75), (49, 1.0)):
        s = math.sin(math.radians(t * 360.0))
        R.pose(f, {
            "spine_02": lean(7.0), "head": lean(10.0) @ turn(6.0 * s),
            "upperarm_l": fwd(40.0 - 6.0 * s), "lowerarm_l": inward("l", 16) @ elbow(80.0 + 8.0 * s),
            "upperarm_r": fwd(40.0 + 6.0 * s), "lowerarm_r": inward("r", 16) @ elbow(80.0 - 8.0 * s),
            "hand_l": q(AY, 18.0 * max(0.0, s)), "hand_r": q(AY, -18.0 * max(0.0, -s)),
            "_grip_l": 45.0, "_grip_r": 45.0,
        }, pelvis_drop=0.005)
    return act


def dig(R):
    act = _action(R.rig, "Dig", 1, 33)
    legs, drop = R.crouch(32.0)
    for f, (sh, el) in ((1, (25, 30)), (9, (70, 60)), (17, (15, 20)), (25, (45, 40)), (33, (25, 30))):
        rots = dict(legs)
        rots.update({"spine_01": lean(14.0), "spine_02": lean(12.0), "head": lean(6.0),
                     "upperarm_l": fwd(sh), "lowerarm_l": inward("l", 14) @ elbow(el),
                     "upperarm_r": fwd(sh + 4), "lowerarm_r": inward("r", 14) @ elbow(el + 4),
                     "_grip_l": 75.0, "_grip_r": 75.0})
        R.pose(f, rots, pelvis_drop=drop)
    return act


def haul(R):
    act = _action(R.rig, "Haul", 1, 33)
    for f, s in ((1, 0.0), (17, 1.0), (33, 0.0)):
        R.pose(f, {"spine_02": lean(-4.0) @ tilt(2.0 * (2 * s - 1)), "head": lean(4.0),
                   "upperarm_l": fwd(48.0), "lowerarm_l": inward("l", 18) @ elbow(78.0),
                   "upperarm_r": fwd(48.0), "lowerarm_r": inward("r", 18) @ elbow(78.0),
                   "_grip_l": 60.0, "_grip_r": 60.0}, pelvis_drop=0.01)
    return act


def charge(R):
    """Docked/resting stand (the robots' clip; crew get a calm variant)."""
    act = _action(R.rig, "Charge", 1, 49)
    for f, t in ((1, 0.0), (25, 1.0), (49, 0.0)):
        R.pose(f, {"spine_03": lean(-0.8 * t), "head": lean(2.0 + t)}, pelvis_drop=0.003 * t)
    return act


def plant(R):
    act = _action(R.rig, "Plant", 1, 33)
    legs, drop = R.crouch(58.0)
    for f, (a, b) in ((1, (40, 40)), (9, (55, 38)), (17, (40, 40)), (25, (38, 55)), (33, (40, 40))):
        rots = dict(legs)
        rots.update({"spine_01": lean(16.0), "spine_02": lean(14.0), "spine_03": lean(8.0), "head": lean(10.0),
                     "upperarm_l": fwd(a), "lowerarm_l": elbow(30.0),
                     "upperarm_r": fwd(b), "lowerarm_r": elbow(30.0)})
        R.pose(f, rots, pelvis_drop=drop)
    return act


def repair(R):
    act = _action(R.rig, "Repair", 1, 33)
    for f, c in ((1, 0.0), (9, 1.0), (17, 0.0), (25, 1.0), (33, 0.0)):
        R.pose(f, {"spine_03": lean(-6.0), "neck_01": lean(-6.0), "head": lean(-10.0),
                   "upperarm_r": fwd(128.0), "lowerarm_r": elbow(35.0 + 25.0 * c),
                   "hand_r": q(AY, 25.0 * c),
                   "upperarm_l": fwd(30.0), "lowerarm_l": elbow(60.0),
                   "_grip_r": 75.0}, pelvis_drop=0.005)
    return act


def operate(R):
    act = _action(R.rig, "Operate", 1, 33)
    for f, (jl, jr) in ((1, (0, 0)), (9, (1, 0)), (17, (0, 0)), (25, (0, 1)), (33, (0, 0))):
        R.pose(f, {"spine_02": lean(5.0), "head": lean(10.0),
                   "upperarm_l": fwd(38.0), "lowerarm_l": inward("l", 16) @ elbow(88.0 + 12.0 * jl),
                   "upperarm_r": fwd(38.0), "lowerarm_r": inward("r", 16) @ elbow(88.0 + 12.0 * jr),
                   "_grip_l": 30.0, "_grip_r": 30.0}, pelvis_drop=0.005)
    return act


def stride_cm(R, act, height_m):
    """Body travel per Walk cycle implied by the planted foot, scaled to the
    1.8 m height the game normalises every walker to."""
    R.rig.animation_data.action = act
    sc = bpy.context.scene
    ys = {}
    for f in (1, 13):
        sc.frame_set(f)
        ys[f] = R.rig.pose.bones["foot_l"].head.y
    sc.frame_set(1)
    step = abs(ys[13] - ys[1])          # left foot's backward travel over half a cycle
    return 2.0 * step * 100.0 * (1.8 / height_m)


def author_all(rig, height_m, log=print):
    R = Rig(rig)
    swing = 20.0
    for _ in range(3):                  # tune the swing to the game's stride
        w = walk(R, swing)
        s = stride_cm(R, w, height_m)
        log("walk swing %.1f deg -> stride %.1f cm at 1.8 m (target %.1f)" % (swing, s, STRIDE_CM_AT_1_8))
        if abs(s - STRIDE_CM_AT_1_8) < 2.0:
            break
        bpy.data.actions.remove(w)
        swing *= STRIDE_CM_AT_1_8 / max(s, 1.0)
    acts = [w, idle(R), workbench(R), worklab(R), dig(R), haul(R), charge(R), plant(R), repair(R), operate(R)]
    rig.animation_data.action = acts[1]
    return acts
