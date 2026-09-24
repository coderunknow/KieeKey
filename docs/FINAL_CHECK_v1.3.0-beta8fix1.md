# Final Check — KieeKey v1.3.0-beta8fix1 ("mất nội dung": the settings page that lost its content)

**Ngày:** 2026-09-24 (Asia/Bangkok)
**Nhánh:** `arena/01a0ce45-kieekey` (PR #33) — trên `9d0bf90` (tag `v1.3.0-beta8`), không rebase, không di chuyển tag
**Commit cuối:** `e036e85` (cây sạch, đã push)
**CI cuối:** run `35993697384` — **success**, cả 4 job: `Native regression (Linux)`, `x64`, `ARM64`, `ARM64EC` (job `Publish release` = skipped, đúng thiết kế: chưa có tag)
**Bản thử:** `out/win-handover/KieeKeyApp.exe`, SHA-256 `fa6b749962583214b01d01ec00f2ec9a65948b4c5b77e2fd546068b96c038ffe` (1.914.880 byte, cross-build x86_64-windows-gnu từ đúng cây `e036e85`)
**Merge:** **CHƯA** · **Tag:** **CHƯA** — chờ bạn xác nhận trên máy Windows (V0–V5 trong `TESTING.txt`)

---

## 1. Dòng bằng chứng CI (câu cần đối chiếu)

```
harness 4320 steps / 4734 assertions / 0 violations []
scenarios 6 run, 0 with violations
invariants (checks/violations) I1:3086/0 I2:33415/0 I3:9042/0 I4:4521/0
          I5:9042/0 I6:108473/0 I8:3/0 I9:342/0 I11:3/0 I12:27590/0 R1:6/0
UI probe: 3456 controls, 14550 checks, 0 findings []
          (native DPI 96, scales 100/125/150, screen checks 27 run / 0 unavailable)
x64 CTest: tests=8; failures=0; disabled=0; skipped=0
x64 job steps: Build (/W4 /WX) ✓ · Run unit tests ✓ · UI probe ✓ · Verify SHA256SUMS manifest ✓
```

Vòng lặp đã đưa từ **1379 vi phạm xuống 0** (theo từng lượt chạy x64):

| run | commit | harness | còn lại |
|---|---|---|---|
| `35959844922` | `77e8fea` | 1379 | I5=58 I6=6 I11=2 I12=1313 |
| `35971507871` | `8ba7da0` | 56 | I1=54 I11=2 |
| `35973718216` | `eb68391` | 84 | I1=81 I11=3 |
| `35979237081` | `e560c89` | 309 | I11=3 I6=306 |
| `35982159995` | `41e0152` | 383 | I1=74 I6=306 I11=3 |
| `35983713630` | `61cd6fd` | 335 | I1=27 I6=306 I11=2 |
| `35985183906` | `f780f5a` | 30 | I1=27 I11=3 (I6=0) |
| `107588927402` | `aebf6e3` | 4 | I1=1 I11=3 |
| `35987071106` | `b354073` | 4 | I1=1 I11=3 |
| `35989916632` | `195c5c5` | 2 | I1=1 I8=1 (I11=0) |
| `35991357693` | `4fc0432` | 1 | I1=1 (I8=0) |
| **`35993697384`** | **`e036e85`** | **0** | **—** |

Bảng bất biến (lượt có vi phạm cao nhất → lượt cuối):

| # | Bất biến | Trước | Cuối | Số lần kiểm ở lượt cuối |
|---|---|---|---|---|
| I1 | thanh tab gấp/mở đo được, hình học trở về đúng | 81 (`eb68391`) | **0** | 3086 |
| I2 | vùng/rect trẻ không đè lên nhau | 0 | **0** | 33415 |
| I3 | không điều khiển nào nằm ngoài trang | 0 | **0** | 9042 |
| I4 | trạng thái cuộn nằm trong khoảng hợp lệ | 0 | **0** | 4521 |
| I5 | thanh cuộn: latch và bit style không được lệch | 58 | **0** | 9042 |
| I6 | kế hoạch (plan) và cửa sổ thật khớp nhau | 306 | **0** | 108473 |
| I8 | vẽ lại toàn bộ không được đổi một pixel | 1 (`195c5c5`) | **0** | 3 |
| I9 | tab đang chọn khớp với vùng nhìn thấy | 0 | **0** | 342 |
| I11 | trang phải có mực (không chỉ nền) | 3 | **0** | 3 |
| I12 | nhãn/nội dung không được cắt cụt | 1313 | **0** | 27590 |
| R1 | mỗi lượt đo tự dọn trạng thái của nó | 0 | **0** | 6 |

Audit bố cục: `1425` finding (`77e8fea`) → **0** (`win32_rect 14→0`, `overlap 4→0`, `clip=0`, `outside_page=0`, `hittest=0`).

---

## 2. Nguyên nhân gốc theo từng lớp lỗi

Hai nguyên nhân gốc giải thích **triệu chứng bạn báo** ("mất nội dung", thanh cuộn biến mất):

1. **BS-18 — quên solve sau khi đổi tỉ lệ.** `applySettingsDpiScale()` nhân lại toàn bộ
   hình chữ nhật con, xoá baseline bố cục (`g_settingsScroll.solved`) rồi dừng; đường
   `WM_DISPLAYCHANGE → refreshSettingsDpi() → applySettingsDpiScale()` không hề gọi
   solver. Hộp thoại ở lại trạng thái "đã đổi tỉ lệ nhưng chưa được bố cục": trang mất
   nội dung, thanh cuộn không còn gì để cuộn. Sửa: đường rescale tự solve; và
   (BS-18 phần hai) đưa trang về baseline **trước khi** ghi lại hình chữ nhật — nếu
   không, offset cuộn bị nhân vào hình học và thành vĩnh viễn.

2. **BS-22s — tab control nằm TRÊN các control của trang (z-order).** Control tab được
   tạo trước và sở hữu toàn bộ hình chữ nhật của trang; các control nội dung nằm trên
   nó về thứ tự chồng (z-order). Thứ tự đó **không cố định**: mỗi lần `showTab()` gọi
   `SW_SHOW` cho các control của tab đang chọn — và một cửa sổ được đưa lên trên sẽ
   mang theo cả hình chữ nhật của nó. Khi control tab ở trên một control nội dung,
   control đó vẫn còn nguyên (đúng rect, `WS_VISIBLE`, không region, `parent dlg`) và
   **đơn giản là không được vẽ**: người dùng thấy nền hộp thoại ở chỗ đáng ra là một
   hàng cài đặt — đúng ảnh bạn gửi — trong khi mọi phép kiểm trạng thái cửa sổ đều
   xanh. Lượt `35988174121` đo được đúng trạng thái đó: 8 control của một tab có
   `painted 0/3` tại chính rect của chúng, trong khi phần chrome (ngoài rect của tab)
   vẫn vẽ. Sửa: sau khi solver áp xong mọi hình chữ nhật, control tab được đưa xuống
   **đáy** thứ tự chồng (`HWND_BOTTOM`, chỉ đổi z-order — không move/size/activate), và
   probe ghi lại vị trí z-order (`z N tabAbove|tabBelow`) của từng control bị đánh giá.

Các lớp lỗi còn lại (đều là những đường dẫn tới cùng triệu chứng, hoặc lỗi đo lường
làm CI không nhìn thấy triệu chứng):

| Mã | Nguyên nhân gốc | Bất biến |
|---|---|---|
| BS-22 | solver đọc hình chữ nhật **live** (đã cuộn/đã rescale) thay vì hình học **authored** | I5, I12 |
| BS-22c | clamp chỉ được thu hẹp; region phải theo cửa sổ; một chủ duy nhất cho cặp latch/`WS_VSCROLL` | I5 |
| BS-22d/23 | tab phải tự bố cục lại **cả hai chiều**; một hàng phải vừa đúng chữ của nó theo chiều rộng | I1, I12 |
| BS-22e | thanh cuộn thuộc về cửa sổ nó phục vụ; một lần đổi cỡ chữ phải huỷ được | I5 |
| BS-22f | nhà máy font phải trả đúng mặt chữ được yêu cầu; đường đổi cỡ chữ phải biết mọi mặt chữ | I1 |
| BS-22g/h/i | phép đo phải **chứng minh tiền đề** của nó (hiệu chuẩn client cho chu trình tab; nguồn gốc khung hình chụp; các lượt không đo được phải tự báo) | I1, I11 |
| BS-22j/k | nhãn không vừa thì **gấp dòng**, không được cắt; khung hình chụp phải chứng minh nó là khung của mình | I6, I12 |
| BS-22l/o/p/q | plan phải mô tả **cửa sổ thật** theo cả hai chiều (chiều cao combo do cửa sổ quyết định; số hàng do control quyết định) | I6, I1 |
| BS-22r | phép đối chiếu render tính pixel của DIB chưa vẽ là "mực" | I11 (đo) |
| BS-22t | số hàng phải đọc **theo cả hai chiều** (hàng có thể xếp lên trên); và vùng pixel lộ ra sau khi đổi z-order phải được **vẽ lại đồng bộ** | I1, I8 |
| BS-22u | chu trình tab phải đọc trạng thái **sau khi app đã lắng** và báo ra **số học của plan** (need/available/client) | I1 (đo) |
| BS-22v | nhãn tab phải được đo bằng **font mà control thật sự vẽ** (`WM_GETFONT`), không phải `uiFont()` | **I1 (chốt)** |

BS-22v là mắt xích cuối: `solveSettingsLayout()` là phép đo **duy nhất** trong solver
chọn `uiFont()` thay vì font của control (hai helper đo còn lại —
`measureSingleLineWidthPx()`, `measureStaticTextHeightPx()` — đã dùng `WM_GETFONT`).
Khi cỡ chữ khác 100 %, plan đo nhãn ở mặt chữ 13 px trong khi control vẽ mặt 19 px:
plan trả lời "một hàng" cho một thanh tab mà control đã gấp thành hai, `stripRows` và
bit style mô tả một thanh tab không tồn tại trên màn hình, và **quá trình gấp/mở không
thể được hoạch định** — nên lượt native không đo được chuyển tiếp (`not measurable`).
Bằng chứng số nằm trong ghi chú của lượt `68544ea`:
`a1:700/683 plan1(559/643) ctl1 ml0` cùng `font` hai bên.

---

## 3. Cổng kiểm (cây `e036e85`)

| Cổng | Lệnh | Kết quả |
|---|---|---|
| Bộ kiểm native | `tests/run_all_tests.sh` | **ALL NATIVE TESTS PASSED** |
| Audit bố cục | `python3 scripts/audit_layout.py --strict` | **AUDIT OK** (0 finding) |
| Luật vẽ | `python3 scripts/check_dialog_paint_rules.py` | **OK** — 11 luật |
| Seed kiểm toán | `python3 tests/verify_audit_seeds.py` | **ALL SEEDED VIOLATIONS CAUGHT** (mọi seed BS-22x) |
| Hình dạng báo cáo probe | `python3 tests/check_probe_json_shape.py` | **OK** (+ self-test) |
| Phiên bản | `python3 scripts/check_version.py` | **OK** — `1.3.0-beta8fix1` (PE `1.3.0.10`) |
| SHA256SUMS | `bash scripts/gen_sha256sums.sh --check` | **in sync** |
| Cảnh báo biên dịch | `zig c++ -Wall -Wextra -Wshadow` | **0 cảnh báo** (main + probe) |
| CI x64 (MSVC `/W4 /WX`) | job `x64`, run `35993697384` | **success** — build, unit tests, UI probe, SHA manifest |
| CI ARM64 / ARM64EC | cùng run | **success** (build) |
| CI Native (Linux) | cùng run | **success** — bộ regression đầy đủ |

---

## 4. UNVERIFIED (Windows) — những gì CI **không** chứng minh được

* **Triệu chứng của bạn đã hết hay chưa.** CI chứng minh trạng thái gây ra nó (control
  bị che bởi z-order, và đường đổi tỉ lệ không solve) **tái hiện được và đã bị sửa**;
  nhưng chỉ máy Windows của bạn mới xác nhận được là nó không còn xảy ra. Đây là lý do
  bản này **chưa merge, chưa tag**.
* Nhánh nào trên máy bạn đưa tới lỗi (đổi tỉ lệ màn hình, mở lại hộp thoại, nhiều màn
  hình, text size 125/150 %) là **UNVERIFIED** — bản sửa loại bỏ cả **lớp** (không còn
  đường nào để tab control ở trên nội dung; không còn đường nào rescale mà không solve).
* Cỡ chữ 125 %/150 % và DPI 120/144 trong CI được điều khiển qua đúng đường của app
  (font factory + re-solve), không phải bằng một màn hình thật ở tỉ lệ đó.
* Bản thử là **cross-build** (zig/MinGW) từ cùng cây mã đã chạy CI — bản phát hành
  chính thức (MSVC, x64/ARM64/ARM64EC) sẽ được build bởi CI khi có tag.

## 5. Việc còn lại (chờ bạn)

1. Chạy `TESTING.txt` (V0–V5) trên máy bạn với `KieeKeyApp.exe` + `SHA256SUMS.txt` đi kèm.
2. Nếu mọi ô đạt: nhắn **"ok, merge"** → tôi merge PR #33 và tag `v1.3.0-beta8fix1`.
3. Nếu còn sai: gửi ảnh chụp + chuỗi băm + bước thao tác; tôi mở lại vòng chẩn đoán với
   đúng trạng thái đó (không đoán).
