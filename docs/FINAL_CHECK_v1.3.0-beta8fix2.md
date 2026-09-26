# Final Check — KieeKey v1.3.0-beta8fix2 ("chữ bị nhân bản": the ghosts of the previous frame)

**Ngày:** 2026-09-26 (Asia/Bangkok)
**Nhánh:** `arena/01a0d7f1-kieekey` (PR #35) — đã merge vào `main` thành `49af9a0`
**Tag:** `v1.3.0-beta8fix2` trên `49af9a0` (build phát hành: run `36206791036`)
**Trạng thái phát hành:** **UNVERIFIED (Windows)** — CI xanh toàn bộ; hành vi trên máy thật chờ người dùng xác nhận (T1–T5 trong `TESTING_v1.3.0-beta8fix2.txt`)

---

## 1. Dòng bằng chứng CI (7 vòng đo + bản vá + bump phiên bản)

| vòng | trục đo | run | verdict |
|---|---|---|---|
| 1 | open path mặc định + tab mở từ khay (I13/I14/I15) | `36122792718` | 0 violations |
| 2 | discriminants: tick, growth reflow, WM_DPICHANGED thật, bar flip-flop (E1–E4) | `36123983333` | 0 violations |
| 3 | full-height + width sweep có bar di chuyển (E5) | `36134839800` | 0 violations |
| 4 | audit pixel tại hình học mở mặc định, từng tab (E6) | `36137870873` | 0 findings (54 screen checks) |
| 5 | classic vs themed (E7) + host facts của runner | `36139368885` | hình học y hệt, 0 findings (81) |
| 6 | scroll round-trip tổng hợp + audit giữa chừng (E8) | `36203136100` | 0 findings (85) |
| 7 | **kéo thumb THẬT bằng SendInput** (E9) | `36204704672` | 0 findings (89) |
| vá | **BS-23d** repaint toàn cây | `36205419738` | 4/4 success |
| phiên bản | mọi carrier = 1.3.0-beta8fix2 / PE 1.3.0.11 | `36206247560` | 4/4 success |

Digest cây cuối cùng (run `36206247560`, job x64):

```
harness 4320 steps / 5196 assertions / 0 violations []
scenarios 9 run, 0 with violations
invariants (checks/violations) I1:3685/0 I2:53085/0 I3:10074/0 I4:5037/0
          I5:10074/0 I6:106761/0 I8:3/0 I9:342/0 I11:6/0 I12:35134/0
          I13:609477/0 I14:20148/0 I15:53085/0 R1:6/0
UI probe: 3456 controls, 16963 checks, 0 findings []
          (native DPI 96, scales 100/125/150, screen checks 89 run / 0 unavailable)
host: win 10.0.20348 appThemed 1 themeActive 1 dwm 1 native dpi 96
drag @144 dpi t0: out 12xLINEDOWN+PAGEDOWN+BOTTOM back TOP, offset 0/227
thumb @144 dpi: REAL drag to offset 227/227
```

## 2. Sự kiện người dùng (Phase 0, 3 lượt) — nguồn của hồ sơ này

* Ảnh beta8fix1 @150%, cỡ mặc định: tiêu đề vẽ 2 lần chồng groupbox tab Cấp độ (F1), nội dung tab cũ đè tab mới (F2), tràn mép phải dù có thanh cuộn (F3/F6), strip 2 hàng (F4), page bắt đầu phía trên vùng nhìn (F5).
* Lỗi **tồn tại qua đóng–mở lại**; xuất hiện khi **bấm tab**, nặng nhất ở tab **Cấp độ**; một màn hình 150% từ lúc khởi động; **Win 10 LTSC 21H2 (19044)**, x64.
* Dialog **không resize được** (đúng thiết kế: không `WS_THICKFRAME`) → "kéo" là kéo **thanh cuộn**; **"kéo thì các chữ bị nhân bản"**.

## 3. Bản vá duy nhất — BS-23d (presentation layer)

`settingsRepaintAll` chuyển từ `InvalidateRect(NULL,TRUE)+UpdateWindow` sang
`RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE |
RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME)` — đúng primitive mà audit pixel
của probe (`checkStalePixels`) chứng minh khôi phục khung hình sạch. Một lời
gọi sau mỗi thay đổi trạng thái (showTab, applySettingsScrollOffset,
solveSettingsLayout); không vòng lặp, không timer; là tập cha của cặp cũ theo
cấu tạo. Ghim bằng paint rule 3 + seed mutation (110 seeds caught).

**Không có cặp run-id đỏ→xanh** — sau 7 vòng đo không trạng thái nào CI với
tới được FAIL trên cây beta8fix1; bản vá đi ra theo cơ chế (mechanism), ghi
rõ UNVERIFIED (Windows). Toàn bộ lập luận + số đo: `docs/HYPOTHESES_BS23_v1.3.0-beta8fix2.md`.

## 4. Đối chiếu tiêu chí nghiệm thu của master prompt

1. Mỗi F1–F6 ≥1 invariant/scenario mới: E1–E9 + I13–I15 bao phủ cả 6 lớp lỗi (mở/resize/DPI/strip/scroll/pixel); **không có run-id đỏ** vì 7 vòng đo chứng minh không trạng thái nào hỏng trên runner — độ lệch so với yêu cầu ghi công khai ở mục 3.
2. Fuzz 0 violations, mọi gate cũ xanh, version gate đồng ý `1.3.0-beta8fix2`: **đạt** (digest mục 1).
3. Release `v1.3.0-beta8fix2` với 3 tài sản + tài liệu bàn giao SHA khớp: run tag `36206791036` (x64 / ARM64 / ARM64EC).
4. Người dùng xác nhận UI sạch trên máy thật: **CHỜ** — cho tới lúc đó mọi tuyên bố "đã sửa" đều mang nhãn UNVERIFIED (Windows).

## 5. Những gì KHÔNG đổi

Không đụng engine/hook/TSF; `check_input_isolation.py` xanh; không
`WS_EX_COMPOSITED`, không vòng lặp repaint, không timer mới, không vô hiệu
scrollbar, không xử lý `WM_MOUSEWHEEL` ngoài `comboWheelProc`; mọi file giữ
GPL-3.0 + SPDX; cây sạch, `SHA256SUMS.txt` đồng bộ.
