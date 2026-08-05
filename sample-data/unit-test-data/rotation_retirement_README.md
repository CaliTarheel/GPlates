# D6 rotation-retirement fixture

Use `rotation_retirement.rot` to exercise **Retire plate toward the present** without changing a project file.

1. Open GPlates and load `rotation_retirement.rot`.
2. Set the reconstruction time to **50 Ma**.
3. Choose **Reconstruction > Edit Rotation File...**.
4. Select `rotation_retirement.rot`, choose **Retire plate toward the present**, and enter moving Plate ID **701**.
5. Select **Apply**, review the destructive-change confirmation, and confirm it.
6. Save the rotation file to a temporary location and inspect the saved text.

Expected result:

- Plate 701 has no active rotation history younger than 50 Ma.
- Its Plate 702 crossing sequence retains the 75 and 100 Ma poles and gains an active 50 Ma boundary pole whose comment contains `GreaterPlates: retired toward present`.
- Its younger Plate 000 parent sequence containing only 0 and 40 Ma poles is removed.
- Parent Plate 702 and control Plates 703 and 704 are unchanged.
- At 75 Ma, Plate 701 is present in the reconstruction tree; at 25 Ma, it is absent.
- One **Edit > Undo** restores the original younger history and the removed sequence.

Cancel the confirmation in a second run to verify that it makes no changes.
