"""Revision 3 placement: ref -> (x mm, y mm, rotation degrees).

Origin is the top-left board corner; y increases downward. The XIAO USB-C end
hangs over the right edge. U1 lies flat across the
bottom middle with its pins at the left end, next to the input protection. TP1-TP8 are mounted on the back by build_pcb.py.
"""

BOARD_W, BOARD_H = 58.0, 24.0

PLACE = {'J1': (2.5, 19.5, 180),
 'R1': (6.5, 2.4, 180),
 'U2': (8.5, 8.3, 180),
 'C2': (3.0, 7.6, 90),
 'C4': (13.4, 8.0, 90),
 'U3': (16.8, 6.8, 180),
 'C5': (16.8, 3.6, 0),
 'R3': (25.2, 4.4, 90),
 'R6': (27.5, 3.6, 0),
 'R7': (27.5, 5.6, 0),
 'C6': (27.5, 7.6, 180),
 'F1': (9.4, 21.8, 0),
 'D1': (9.4, 18.0, 0),
 'D2': (9.4, 13.7, 0),
 'C1': (17.2, 10.3, 0),
 'U1': (16.0, 15.45, 270),
 'C3': (29.8, 10.2, 0),
 'D3': (33.6, 10.2, 0),
 'R4': (32.8, 5.2, 0),
 'D5': (35.5, 7.7, 90),
 'U4': (47.5, 12.0, 270),
 'Q1': (22.4, 2.0, 0),
 'R10': (19.4, 2.6, 90),
 'R11': (26.0, 1.4, 0),
 'U6': (21.8, 8.6, 0),
 'C8': (21.8, 6.6, 0)}

# Reference text offsets in board coordinates.
REF_OFFSET = {'J1': (0, 2.2),
 'R1': (0, 0),
 'U2': (0, 0),
 'C2': (0, -2.2),
 'C4': (0, -2.2),
 'U3': (2.9, 0),
 'C5': (-2.4, 0),
 'R3': (-1.6, 0),
 'R6': (2.4, 0),
 'R7': (2.4, 0),
 'C6': (2.4, 0),
 'F1': (0, 0),
 'D1': (0, 0),
 'D2': (0, 0),
 'C1': (3.0, 0),
 'U1': (10, 2.5),
 'C3': (0, -1.6),
 'D3': (2.5, 0),
 'R4': (0.2, -1.25),
 'D5': (3.2, 0),
 'U4': (-8.0, 0),
 'Q1': (0, 2.6),
 'R10': (0, 2.4),
 'R11': (2.6, 0),
 'U6': (2.6, 0),
 'C8': (2.4, 0)}

for i in range(1, 9):
    PLACE[f"TP{i}"] = (7.0 + (i - 1) * 6.0, 12.0, 0)
    REF_OFFSET[f"TP{i}"] = (0, 1.6)

# Dedicated return to the back plane, relative to the footprint origin, so a
# ring of signal tracks cannot isolate U6's ground.
GND_STITCH = {'U6': (-2.0, 1.0)}
