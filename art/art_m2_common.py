"""art_m2_common.py - shared imports + small helpers for the Milestone-2 art modules (task M4)."""
import math  # noqa: F401

from artlib import (T, KZ, STOREY, clamp, lerp, ihash, vnoise, vnoise3, local_st, Model, Solid,  # noqa: F401
                    Convex, box, gable_body, hip_body, roof_slab, gable_roof, mono_body, Cone, Ellipsoid,
                    HeightField)
from art_terrain import g_grass, g_lot, g_water, g_road, g_rail  # noqa: F401
from art_models import *  # noqa: F401,F403
from art_models import (m_flat, in_rect, window_px, wins_row, m_house, m_roof, m_hiproof, m_chimney,  # noqa: F401
                        m_block, m_balcony, m_brickwall, m_corr, m_cone_brick, m_concrete_round, m_leaf,
                        m_trunk, m_spruce, m_rock, m_flag, m_awning, m_saw, m_timber, m_logs, m_woodpile,
                        m_concrete, m_beams, m_concrete_pipe, m_rock2, new_grp)


def _v(fn, *a, **k):
    """deferred builder (same as gen_art._v)"""
    return lambda: fn(*a, **k)


def add_colours(cols):
    """register new lit base colours (name -> '#rrggbb') for a Milestone-2 module without editing artlib.py:
    updates PAL_BASE and the already-built STYLE_C palette (same saturation rule as artlib.Style)."""
    import artlib
    from artlib import hexc
    st = artlib.STYLE_C
    for k, v in cols.items():
        if k in artlib.PAL_BASE and artlib.PAL_BASE[k].lower() != v.lower():
            raise ValueError('colour %s already defined as %s' % (k, artlib.PAL_BASE[k]))
        artlib.PAL_BASE[k] = v
        c = hexc(v)
        g = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
        st.pal[k] = tuple(clamp(g + (x - g) * st.sat, 0, 255) for x in c)
        st.cache.pop(k, None)
