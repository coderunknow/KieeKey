# Kế hoạch BS-24 — v1.3.0-beta8fix3: "chữ bị nhân bản" — điều tra sâu nhất, KHÔNG fix mù

**Ngày:** 2026-09-26 · **Nhánh:** `arena/01a0db54-kieekey` (từ `main` @ 8a31d9f = cây beta8fix2)
**Trạng thái:** **PHASE 0 — chờ người dùng duyệt kế hoạch + trả lời bộ câu hỏi.** Chưa sửa một dòng code app nào.

---

## 0. Định danh & định nghĩa XONG

- Phiên bản đích: **v1.3.0-beta8fix3**, PE **1.3.0.12** (mặc định theo dòng
  beta8fix1 → beta8fix2 → beta8fix3; chờ xác nhận — câu hỏi cuối).
- XONG = (a) người dùng chạy bản mới **trên đúng cái máy đã báo lỗi**, làm bộ
  test T1–T5, gửi ảnh TOÀN BỘ cửa sổ (kể cả thanh tiêu đề) và ảnh SẠCH;
  (b) CI 4/4 xanh, fuzz 0 violations, mọi gate cũ xanh, version gate đồng ý;
  (c) release 3 tài sản + probe + tài liệu bàn giao SHA khớp.
- Mọi tuyên bố "đã sửa" khi chưa có (a) đều mang nhãn **UNVERIFIED (Windows)**.

## 1. Đã làm trong vòng này (Phase 0, không chạm code app)

1. **Đọc hết bộ hồ sơ:** toàn bộ dialog trong `src/app/main.cpp` (showTab,
   solveSettingsLayout, applySettingsScrollOffset, settingsRepaintAll,
   settingsAdoptScrollbarVisibility, settingsApplyScrollbarLatch,
   WM_VSCROLL/WM_CREATE/WM_SIZE/WM_DPICHANGED/WM_DISPLAYCHANGE/TCN_SELCHANGE),
   `DialogLayout.hpp`, `kieekey_core.hpp`, `tools/ui_probe/ui_probe.cpp` (I1–I15,
   E1–E9, checkStalePixels, captureWindow, host facts),
   `scripts/check_dialog_paint_rules.py` (17 nhóm rule),
   `scripts/audit_layout.py`, `tests/verify_audit_seeds.py`,
   `scripts/check_version.py`, `.github/workflows/build.yml`,
   `docs/HYPOTHESES_BS23_v1.3.0-beta8fix2.md`, `FINAL_CHECK_*beta8fix2.md`,
   release notes + TESTING beta8fix1/beta8fix2, BUG_HUNT beta8 (BS-13, BS-16a–e,
   BS-17, BS-18, RS-06), CHANGELOG beta8/beta8fix1/beta8fix2.
2. **Đóng gói probe gửi máy thật (bắt buộc §5.1):** `/home/user/handoff/`
   - `kieekey_ui_probe.exe` — PE32+ x64 console, SHA-256
     `38aef72e…e9cb5`, dựng từ **đúng cây beta8fix2 (1.3.0.11) chưa sửa gì**,
     bằng đường cross-build được repo ghi nhận (`scripts/build_windows_exe.sh`,
     zig 0.16.0). Nó mở đúng dialog thật qua đường tạo cửa sổ của app,
     **PerMonitorV2** (gọi `SetProcessDpiAwarenessContext` trong `main()` của
     probe — đã đọc code, không phải phỏng đoán), pass đầu = scale native của
     máy (trên máy của bạn = đúng 150%), sau đó 100/125/150%. Không hook,
     không tray, không singleton → chạy song song an toàn với app đang chạy.
     Từ chối chạy nếu mirror struct scroll lệch với app.
   - `collect_report.ps1` + `run_probe_and_report.bat` — chạy probe, tự thu:
     OS build, **LogPixels** (H5), DWM/theme/contrast (H4/H6), GPU + **version
     driver** (H6), `DragFullWindows` ("Show window contents while dragging",
     H6), **số tiến trình KieeKeyApp.exe + đường dẫn + File version + SHA-256
     từng tiến trình** (H1/H7), tasklist đầy đủ (H2), **danh sách cửa sổ đang
     phủ lên vùng dialog** (H2), rồi zip tất cả.
   - `HUONG_DAN_thu_thap_bang_chung.txt` (1 trang), `BIEN_BAN_HIENTRUONG_BS24.txt`
     (mẫu điền), `PROBE_SHA256.txt` (provenance đầy đủ).
3. **Bổ sung sự kiện (đọc code):** thanh tiêu đề dialog là
   `KieeKey — Cài đặt & Thông tin  [<SHA-8 của exe>]` (RS-06) — nhãn phiên bản
   `KieeKey v1.3.0-beta8fix2` nằm trong header của dialog (đúng STATIC bị vẽ
   hai lần trong F1). Báo cáo chẩn đoán (tab Chẩn đoán) đã có khối build
   identity (bản + PE version + SHA-256 64 ký tự + đường dẫn) — nền tảng để
   mở rộng ở §5.2.
4. **Kiểm tra khả năng hạ tầng:** sandbox này KHÔNG với tới Azure artifact
   storage → không tải được probe do MSVC CI dựng (run 36206247560); zig
   cross-build là con đường có sẵn trong repo. Ghi vào hạn chế (§8).

## 1b. Phase 0 — câu trả lời người dùng (2026-09-26, lượt 1)

| # | Câu hỏi | Trả lời | Tác động giả thuyết |
|---|---|---|---|
| 1 | Tiêu đề dialog + header | Tiêu đề `KieeKey — Cài đặt & Thông tin [..]`; header ghi **v1.3.0-beta8fix2** | H1: UI đang chỉ đúng phiên bản (mã SHA trong [..] chưa được sao lại — `system-facts.txt` trong zip probe sẽ khớp byte-level) |
| 2 | File version exe đang chạy | **1.3.0.11** | H1: gần loại (chờ khớp SHA-256 với release) |
| 3 | Số tiến trình KieeKeyApp.exe | **1** | H7: yếu (không có 2 tiến trình) |
| 4 | IME/overlay + clean boot | Chỉ KieeKey; **CHƯA clean boot** | H2: **CHƯA KIỂM TRA** — clean boot vẫn là thí nghiệm phân biệt bắt buộc |
| 5 | Scale | **Preset 150% chuẩn** | H5: yếu (LogPixels trong report probe sẽ xác nhận bằng số — phải = 144) |
| 6 | Probe + duyệt kế hoạch | Cần **hướng dẫn từng bước chi tiết hơn** → đã gửi `BUOC_CHAY_PROBE_chi_tiet.txt` | H4: probe chưa chạy — thí nghiệm then chốt còn treo |

**Đánh giá cập nhật:** người dùng đang chạy đúng bản (title + header + File
version 1.3.0.11 + 1 tiến trình + scale preset chuẩn, tự khai không có
overlay). H1/H5/H7 gần như loại (chờ xác nhận byte-level từ zip probe). Hai
trục sống còn lại: **H4 — chạy probe trên máy thật** và **H2 — clean boot**.
Vòng tới là DATA, không phải code.

**Lượt 2 (2026-09-26):** người dùng **DUYỆT kế hoạch** như hiện tại và
**XÁC NHẬN định danh `v1.3.0-beta8fix3` / PE `1.3.0.12`**. Báo cáo probe
**không chạy được** trên máy thật (lý do cụ thể CHƯA có — đã yêu cầu copy
nguyên văn lỗi console; file hướng dẫn đã bổ sung nhánh "lỗi thì sao" + cách
chạy PowerShell tay để bắt lỗi). Khi có mô tả lỗi: (i) lỗi ở file tải về/
SmartScreen/AV → xử lý vận chuyển; (ii) lỗi PowerShell (dòng đỏ `At ...ps1`)
→ sửa `collect_report.ps1`; (iii) probe crash (exit code khác 0 / hộp lỗi
Windows) → đó là dữ liệu thật đầu tiên về hành vi cross-build trên 19044 —
lấy exit code + dòng đầu `console.log` + (nếu có) code lỗi Windows
(0xc00000xx) để khoanh vùng.

**Lượt 3 (2026-09-26) — CHUYỂN HƯỚNG QUAN TRỌNG:** "lỗi" không phải lỗi
kỹ thuật — người dùng **không biết cách chạy** (4 file rời rạc + console +
SmartScreen là quá nhiều rào chắn) và việc tải file rời từ chat viewer cũng
bất tiện. Hành động: (1) đóng gói **MỘT zip duy nhất**
(`kieekey-ui-probe-handoff-win-x64.zip`: bat + ps1 + exe + pdb +
`READ_ME_TRUOC_1_phang.txt` 3 bước) — chỉ cần giải nén và đúp chuột vào 1
file; (2) sandbox không upload được tài sản release
(`uploads.github.com` bị chặn) → zip được commit lên nhánh
`arena/01a0db54-kieekey` (commit `fea838e`) làm kênh tải bền:
`https://github.com/coderunknow/KieeKey/raw/arena/01a0db54-kieekey/handoff/kieekey-ui-probe-handoff-win-x64.zip`
— artifact TẠM, gỡ sau Phase 1; (3) mọi hướng dẫn rút xuống "3 việc".

**Lượt 4 (2026-09-26) — PROBE ĐANG CHẠY TRÊN MÁY THẬT:** người dùng tải
zip thành công, giải nén, đúp chuột `run_probe_and_report.bat` — cửa sổ
Cài đặt tự mở, tự chuyển tab (trạng thái "chạy tốt"). Đang chờ dòng
"DONE." + file `kieekey-report-<thời-gian>.zip` + ảnh S1–S3 ở lượt chat kế.
Công cụ phân tích đã sẵn sàng: `handoff/analyze_report.py`
(exit 0 = SẠCH trên máy thật, exit 2 = RED trên máy thật = mỏ neo Phase
2→3). Clean boot (H2) CHỜ KẾT QUẢ PROBE — không làm trước để giữ baseline
sạch. Không code app nào cho tới khi có data.

## 2. Sự kiện người dùng (nguyên văn, tổng hợp từ 4 vòng)

- Máy: **Win 10 LTSC 21H2 (19044)**, x64, 1 màn hình, scale **150% từ lúc boot**.
- beta8fix1 @150%, cỡ mặc định, vừa bấm tab "Cấp độ": **F1** tiêu đề vẽ 2 lần
  (một bản thấp ~150px chồng groupbox), **F2** tab chọn là Cấp độ nhưng thân
  trang hiện nội dung Thông tin, **F3/F6** tràn mép phải (cắt giữa ký tự) dù
  thanh cuộn đang hiện, **F4** strip 2 hàng, **F5** nội dung bắt đầu trên vùng
  nhìn.
- Vòng 2: lỗi **tồn tại qua đóng–mở lại**; **bấm tab thì bị đè, mất nội dung**,
  nặng nhất tab Cấp độ.
- Vòng 3: "không resize được" (đúng thiết kế — không `WS_THICKFRAME`) → "kéo"
  là kéo **thanh cuộn**; **"kéo thì các chữ bị nhân bản"**.
- Vòng 4 (beta8fix2, bản vá BS-23d repaint toàn cây): **"Không khác gì hết.
  Lỗi tương tự."** → repaint superset BỊ BÁC BỎ trên máy thật.

**Hệ quả logic đã chốt (tuân thủ, không đo lại):** sau 7 vòng CI (open path
@100/125/150, tray-open tabs, tick thật, growth reflow, WM_DPICHANGED thật,
full-height + width sweep, classic vs themed, scroll round-trip, kéo thumb THẬT
bằng SendInput — 4320 steps / 5196 assertions / 0 violations; 89 screen pixel
audits / 0 findings; `host: win 10.0.20348 ... native dpi 96`) mà bản vá
`RedrawWindow(RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW|RDW_FRAME)`
vẫn không sửa được máy thật → **bóng ma KHÔNG PHẢI vùng invalid bị sót trong
cây cửa sổ của dialog trên cùng một tiến trình.** Thủ phạm ở lớp khác:
sai tiến trình/sai bản (H1/H7), phần mềm phủ (H2), cửa sổ phụ của chính app
(H3), đặc thù build 19044/môi trường render (H4/H5/H6), hoặc đường mở không qua
settle (H8).

## 3. Giả thuyết & thí nghiệm phân biệt (mỗi cái có data riêng, verdict bằng số đo)

| H | Nội dung | Thí nghiệm phân biệt | Ai chạy | Data chốt |
|---|---|---|---|---|
| **H1** | Chạy KHÔNG PHẢI bản mới, hoặc HAI tiến trình (mutex version-free: exe mới chỉ "đánh thức" tiến trình cũ — tray/dialog có thể là của bản CŨ) | Câu 1–3 + `system-facts.txt`: File version, số tiến trình, SHA-256 đối chiếu release, mã [..] trong tiêu đề | Máy thật (đã có sẵn trong gói) | File version ≠ 1.3.0.11 **hoặc** ≥2 tiến trình **hoặc** SHA lệch → H1 xác nhận. DỪNG ở đây: không fix app, xử lý bằng hướng dẫn tắt tiến trình cũ / sửa wake path nếu chỉ đích danh |
| **H7** | Singleton/wake path (liên đới H1) | Cùng bước H1: đếm tiến trình trước/sau khi chạy lại exe; ai "nhận" event wake | Máy thật | 2 tiến trình cùng hook → chỉ đích danh đường wake (code: `kSingletonMutexName` version-free — đọc rồi) |
| **H2** | Overlay/IME khác (EVKey, Unikey, GoTiengViet, OBS, Magnifier, ExplorerPatcher, AV) | (i) Clean boot → hết lỗi? (ii) Window map trong collector: cửa sổ nào phủ vùng dialog; tasklist | Máy thật | Clean boot sạch + list chỉ đích danh một process → H2. Kèm: Unikey từng xuất hiện trong ảnh (cảnh báo unikeynt.exe) — hỏi cụ thể Unikey có bật không |
| **H3** | Cửa sổ phụ CỦA CHÍNH KieeKey vẽ chồng | Đọc code (đã làm): đúng 5 `CreateWindowExW` ngoài dialog/main: tooltip `g_rowTip` (**WS_EX_TOPMOST**, con của dialog — ứng viên duy nhất có thể bay ra ngoài vùng), Arcade/ChaosLab windows (chỉ vẽ trong WM_PAINT của chính nó). TSF: không tạo cửa sổ KieeKey (candidate window là của Windows). Thí nghiệm: window map lúc dialog app đang MỞ + lỗi; probe chạy song song với app (H3-live) | Máy thật (map) + đọc code (xong) | Window lạ có class KieeKey* hoặc tooltip ở vị trí sai trong ảnh lỗi → H3. Nếu không: H3 loại |
| **H4** | Bug Win32 đặc thù build 19044: region + clip + comctl32, DWM/ClearType/classic thật | **THÍ NGHIỆM THEN CHỐT: chạy `kieekey_ui_probe.exe` TRÊN MÁY NGƯỜI DÙNG.** Probe = cùng class, cùng dialog, cùng code layout, cùng PerMonitorV2.Probe sạch + app hỏng → khác biệt ở tiến trình/môi trường/build (về H1/H2/H7, đừng cố tái hiện CI). Probe hỏng → **RED hợp lệ trên máy thật**: lấy firstViolations (op, seed, dpi, offset, page rect) + PNG tab làm mỏ neo vá có đích. Kèm: system-facts (DWM, theme, contrast, FontSmoothing) | Máy thật (đã đóng gói) | `host: win 10.0.19044 ... native dpi 144` + findings trong `ui_probe.json` |
| **H5** | Custom scaling / DPI ảo (LTSC hay đặt custom trong registry → effective DPI lệch PerMonitorV2) | `LogPixels` (collector đã thu; reg query), host line của probe (`native dpi`), so 3 nguồn DPI (app build được mở rộng ở §5.2); đặt preset chuẩn → sign out/in → test lại | Máy thật | LogPixels ≠ 144, hoặc 3 nguồn DPI lệch nhau → H5. Probe ở scale "native" đo đúng 150% hay khác → số đo nói |
| **H6** | GPU driver / DWM compositing ("nhân bản khi kéo" là triệu chứng driver kinh điển) | `system-facts.txt` (DriverVersion + DriverDate + DragFullWindows); đổi phiên bản driver; bật/tắt "Show window contents while dragging" | Máy thật | Đổi driver → sạch → H6. Không đổi được driver → ghi nhận, qua |
| **H8** | Wake-open / đường mở dialog không qua settle | Chỉ khi H1–H5 chưa giải thích: đo đường `openSettingsDialog(tab)` mở TỪ tray vào tab khác rồi đi bộ sang tab 8 (code: đã đọc — tray open tab 4 "Thông tin" rồi user bấm Cấp độ đúng là kịch bản H2-của-BS-23) | CI (probe có `KieeKeyProbeReopenSettings`) + máy thật (probe live) | Vi phạm mới trong harness mở-từ-tab-khác → RED CI; không → loại |

Nguyên tắc: **mọi verdict cần số đo** (rect, px, run id, đường dẫn, version
driver, LogPixels, host line). Không có số đo = chưa verdict.

## 4. Quy trình bắt buộc (Phase 1 → 7)

- **Phase 0 (vòng này):** gói probe + bộ câu hỏi + kế hoạch này → người dùng.
- **Phase 1 — tái hiện trên MÁY THẬT:** nhận zip probe + S1–S3 + biên bản.
  - Probe **RED** → mỏ neo (findings + PNG + host line) → Phase 2.
  - Probe **clean** mà app hỏng → trục khác biệt là TIẾN TRÌNH/MÔI TRƯỜNG
    (H1/H2/H7) → điều tra wake/singleton/overlay; KHÔNG cố tái hiện CI nữa.
  - Cả H1–H6 chưa chốt → **Phase 1b (tuỳ chọn, trình trước khi code):** bản
    chẩn đoán "khai hiện trường" — bản build riêng cho máy thật (không phải
    release) ghi ra file trạng thái cây dialog (rect/region/visibility/z-order
    của từng child + offset + range + dpi 3 nguồn) SAU MỖI thao tác
    (showTab, WM_VSCROLL, WM_DPICHANGED, tick) — mở rộng RS-06 (build
    identity + self-check đã có sẵn), presentation/diagnostic layer only,
    không đổi hành vi. App của người dùng tự khai chính nó tại khoảnh khắc lỗi.
- **Phase 2 — log giả thuyết** vào `docs/HYPOTHESES_BS24_v1.3.0-beta8fix3.md`:
  từng H, thí nghiệm, số đo, verdict. **Không fix trước verdict.**
- **Phase 3 — vá:** presentation layer được phép; engine/hook/TSF chỉ đổi nếu
  Phase 1/2 chỉ đích danh + có RED. Một BS một commit (BS-24x). Mỗi thay đổi
  kèm invariant FAIL trước / PASS sau + run id — TRỪ khi bằng chứng từ máy
  thật: khi đó trích dẫn bằng chứng máy thật (host line, findings, PNG,
  biên bản) và ghi rõ.
- **Phase 4 — gates:** xanh, không hạ chuẩn, fuzz 0 violations, version gate
  (1.3.0-beta8fix3 / PE 1.3.0.12).
- **Phase 5 — CI loop 4/4** (push `arena/**` không chạy CI → PR vào `main`).
- **Phase 6 — release:** PR → merge → tag `v1.3.0-beta8fix3` → 3 tài sản +
  probe (tài sản thứ tư, CI MSVC build) + tài liệu bàn giao ×2 + TESTING.txt.
- **Phase 7 — báo cáo:** (a) sự kiện, (b) giả thuyết + verdict, (c) bản vá +
  bằng chứng, (d) số CI, (e) giới hạn, (f) yêu cầu T1–T5 + ảnh toàn cửa sổ.

## 5. Công cụ đo mới (bắt buộc §5 master prompt)

1. ✅ Probe đóng gói gửi máy thật — **đã làm trong vòng này** (§1.2).
   Chế độ `--user-report` không cần thêm vào code: `collect_report.ps1` làm
   đúng việc đó bên ngoài probe (zip json+png+systeminfo+tasklist) — giữ
   probe nguyên trạng CI.
2. ⏳ Bản chẩn đoán beta8fix3 (sau Phase 1/2, nếu cần): build identity mở rộng
   + effective DPI 3 nguồn (GetDpiForWindow, GetDeviceCaps(LOGPIXELSX),
   GetSystemMetrics/work area), DWM/theme bật tắt, số tiến trình cùng tên,
   đường dẫn exe — để ảnh chụp/báo cáo tự khai môi trường. Chỉ diagnostic
   layer; không đổi layout/paint/hành vi.
3. ✅ Biên bản hiện trường 1 trang — **đã làm** (§1.2).
4. Gate: mọi thí nghiệm CI mới (nếu Phase 1/2 sinh ra) có needle trong
   `check_dialog_paint_rules.py` + seed trong `verify_audit_seeds.py`.

## 6. Ràng buộc bất khả xâm phạm (tuân thủ đầy đủ, tóm từ master prompt §7)

1. Không làm yếu/gỡ invariant, không đổi assert cho xanh, không bỏ scenario.
2. Không `WS_EX_COMPOSITED`, scroll-zoom, vòng lặp repaint, timer mới, vô
   hiệu scrollbar, `WM_MOUSEWHEEL` ngoài `comboWheelProc`.
3. Không đụng engine/hook/TSF trừ bằng chứng chỉ đích danh;
   `check_input_isolation.py` luôn xanh; không dependency mới.
4. MSVC `/W4 /WX` + zig `-Wall -Wextra -Wshadow` sạch mọi TU, cả hai chế độ
   probe.
5. GPL-3.0 + SPDX header giữ nguyên.
6. Cây sạch; `SHA256SUMS.txt` đồng bộ (`git add -A` → gen → add → commit).
7. Không khẳng định hành vi Windows từ CI — ghi UNVERIFIED (Windows).
8. `tests/repros/` và log `docs/` là lịch sử — không xóa.
9. Mọi kết luận cần số đo. 10. **Không fix mù.**
11. BS-23d không âm thầm revert/"cải tiến" thêm hướng repaint; nếu bỏ, commit
    riêng có lý do bằng bằng chứng máy thật.

## 7. Phân nhánh hành động ngay khi có data người dùng

- **H1/H7 xác nhận** (sai bản / 2 tiến trình) → không cần release: hướng dẫn
  + (nếu wake path thật sự lỗi) fix nhỏ có evidence riêng, ghi UNVERIFIED.
- **Probe RED** → mỏ neo đỏ trên máy thật → fix presentation layer có đích,
  mỗi BS-24x một commit, CI xanh, release beta8fix3, T1–T5 lại.
- **Probe clean + app hỏng, tiến trình đúng 1 bản đúng** → Phase 1b (bản chẩn
  đoán "khai hiện trường") là thí nghiệm phân biệt tiếp theo: so cây thật
  (log từ app lúc lỗi) với cây probe (sạch) → từng trường khác nhau là bằng
  chứng.
- **Clean boot sạch** → H2 → danh sách cửa sổ/threshold process.
- **Tất cả clean** → máy thật là host duy nhất tái hiện được: chuyển sang đo
  từ xa (Phase 1b) thay vì đo từ gần (CI) — cùng kỷ luật: số đo, không đoán.

## 8. Hạn chế trung thực

- Sandbox này không với tới Azure artifact storage → probe gửi đi là bản
  **cross-build zig** (đường đã có trong repo, cùng cây code, provenance trong
  `PROBE_SHA256.txt`), không phải bản MSVC CI. Probe không có VERSIONINFO
  resource (không ảnh hưởng hành vi: DPI world do `SetProcessDpiAwarenessContext`
  quyết, đúng như probe CI); bản probe chính thức trong release beta8fix3 vẫn
  do CI MSVC dựng.
- GitHub runners không có Win10 21H2 (chỉ windows-2022) → **máy của người
  dùng là host Win10 21H2 duy nhất** của dự án này. Mọi kết luận về 19044
  đến từ máy thật, không từ CI.
- CI push `arena/**` không chạy; mọi số CI của beta8fix3 phải qua PR vào main.
- Probe đo dialog "mới" (tick bị freeze sau 700ms/tab trong audit) — dialog
  của người dùng "sống" qua tick 500ms và lịch sử thao tác; đó là lý do
  Phase 1b (khai hiện trường từ app thật) nằm trong kế hoạch ngay từ đầu.
