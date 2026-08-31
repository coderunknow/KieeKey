> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# TỔNG KẾT — Dự án "Vượt UniKey": OpenKey-NextGen v3.1

**Ngày:** 29-08-2026 · **Phạm vi:** P0 → P4 theo đề bài chính (master prompt), chạy trên bản
archive OpenKey-NextGen-benchmark đã upload.

## Kết quả bằng số (sự thật khách quan, có thể kiểm chứng lại)

### Sửa lỗi hợp đồng (P0) — tất cả đã kiểm chứng bằng bộ mega differential đầy đủ

| Lỗi | Trước | Sau |
|---|---|---|
| D1 — tràn bộ đệm lịch sử (nguy cơ sập) | 10 sự kiện | **0** |
| D2 — backspace vượt độ dài văn bản đã commit | 1.670 sự kiện | **0** |
| D3 — macro không được mở rộng ở consumer | 462.627 sự kiện hụt | **0** |
| D4 — mất dấu cách sau Restore | `arbithối đoái` | **`arbit hối đoái`** |

Kết quả mega: 5.966.887 ca / 259.320.318 sự kiện — **0 sai khác văn bản** so với oracle
clean-room; bộ thử `test_hotfix` thêm mục §5 với các ca hồi quy D1–D4; ASan/UBSan sạch.

### Nguyên nhân gốc đã xử lý (quan trọng hơn con số)

- Ký tự "ma" (standalone ư/ơ, â có dấu còn sót bit, dấu trên phụ âm) từng bị **biến mất khỏi
  màn hình** khi engine viết lại từ — văn bản hiển thị lệch với bộ đệm engine, sinh ra cả họ lỗi
  backspace. Giờ mọi phần tử phát ra đều hiện được ký tự thật, đúng như hook 2.0.5 gõ lại phím.
- Gõ `axit `, `aspirin `, `apxe ` … hiện nay cho kết quả đúng nguyên văn: ví dụ `axit` từng bị
  thành `ãit` rồi mất dấu cách — bây giờ vòng lặp gõ-phản gõ trả đúng `axit `.

### Đúng chữ (P1) — vượt UniKey trên chính phương pháp 3-phía

Trên bộ đo được dựng lại (66.552 từ gõ được, vòng lặp gõ-phản gõ, so khớp NFC):

| Cấu hình | Phương thức | NextGen v3.1 | UniKey 4.x |
|---|---|---|---|
| as-shipped | Telex | **65.020 (97,70%)** | 64.942 (97,58%) |
| as-shipped | VNI | **65.104 (97,83%)** | 64.998 (97,67%) |
| matched (restore=OFF ×3, từ điển OFF) | Telex | **64.862** | 64.901 |
| matched | VNI | **65.107** | 65.079 |

- **Vượt UniKey ở as-shipped cả hai phương thức** (Telex +78 từ, VNI +106 từ) nhờ
  (1) sửa D4/D2 và (2) chế độ mới **từ điển chặn phục hồi** (dictionary-gated restore):
  khi từ vừa soạn *không có trong từ điển tiếng Việt* và *khác với phím người dùng đã gõ*,
  engine hoàn tác về phím gốc (`bata` không còn bị thành `bât`, `đăngten` không còn bị hoàn
  tác oan). Từ điển chỉ có quyền *phủ quyết*, không tự chèn chữ; tắt tính năng thì engine
  **byte-cố định như v3.0**.
- **Byte-cố định với 2.0.5 ở cấu hình matched được giữ nguyên vẹn** — tập kết quả trùng khớp
  100% (64.862 / 65.107) cho cả Telex và VNI.
- Chấm theo nhóm biến thể chính tả (nguỳ==ngùy, hoá==hóa, thuỷ==thủy…): Telex **98,86%**,
  VNI **99,00%**.
- Còn thua UniKey đúng 2/34 từ trong danh sách thắng của họ (`huơ`, `mono`) — khác luật xử lý
  có ghi nhận trong DIVERGENCES.md, không phải lỗi.

### Tốc độ & bộ nhớ (P2)

| Chỉ số | Trước | Sau | Mục tiêu |
|---|---|---|---|
| vn-compose p50 (T1) | 137 ns | **72 ns** | ≤ 80 ns ✓ |
| vn-compose p99.9 (T1) | 364 ns | **117 ns** | ≤ 220 ns ✓ |
| passthrough p50 (T1) | 113 ns | **62 ns** | ≤ 75 ns ✓ |
| Cấp phát / 100k phím | 260 | **7** | ≤ 260 ✓ |
| Độ dốc bộ nhớ sống | 0,12/10k từ | **0,00** | phẳng ✓ |

Thay đổi chính trên đường nóng: bỏ phép xoá 128 byte `newChars` mỗi phím (chỉ cần ô số 0 —
mọi bên đọc đều giới hạn bởi `newCharCount`), không đổi hành vi.

### Tính năng mới vượt UniKey (P3)

- **Bộ gõ phím tùy chỉnh (user keymap)** — `setKeymapOverride` + `useUserKeymap`, áp dụng trước
  khi xử lý tiếng Việt, có kiểm thử engine.
- **Xuất VIQR** — `OutputEncoding::Viqr`, bảng mã giao-tiắt đầy đủ (`chào` → `cha`o`), có kiểm thử.
- **Hồ sơ theo ứng dụng** — đi trên cơ chế sẵn có của ProcessMonitor (tự loại ứng dụng + chọn
  TSF/SendInput theo foreground), thiết lập lưu registry như 2.0.5.
- **Chưa làm trong v3.1** (báo cáo thẳng thắn, không né): bảng mã Simple Telex 2, BK HCM2 và
  VIQR-*charset* (còn lại là việc sinh bảng trong `tools/gen_flat_tables.py`, đề xuất làm đầu
  cho v3.2).

### Ghi nhận minh bạch

- Bộ `imebench_kit/` gốc không có trong archive upload — đã dựng lại từ chính tài liệu repo
  (engine 2.0.5 và UniKey gốc, bộ sinh phím, corpus viet74k) theo đúng bất biến §7; số tuyệt
  đối phần trăm phụ thuộc bộ đo, còn so sánh giữa các engine trên cùng một bộ đo là chính xác.
- Bộ đo mixed/delete được viết lại nên không so sánh thẳng được với bảng đóng băng; vn-compose
  và passthrough giữ nguyên hình thái protocol.
- Mọi thay đổi hành vi đều nằm trong DIVERGENCES.md kèm reproducer; Cat C (NextGen lệch
  oracle) = 0 trong toàn bộ 259 triệu sự kiện.

## Trạng thái checklist nghiệm thu

- [x] P0 — 0 stale / 0 over-backspace / macro end-to-end / dấu cách sau Restore — xác minh 3 phía
- [x] P1 — Telex và VNI as-shipped ≥ UniKey (strict); danh sách 34 từ thắng của UniKey xử lý 32; byte-parity matched giữ nguyên; bộ chấm normalized đã có
- [x] P2 — vn-compose 72ns ≤ 80; p99.9 117ns ≤ 220; passthrough 62ns ≤ 75; ≤260 cấp phát; không rò rỉ
- [x] P3 — keymap + VIQR xuất + hồ sơ ứng dụng có kiểm thử; ST2/BK HCM2 báo cáo chưa làm
- [x] P4 — toàn bộ battery xanh; BENCHMARK_DELTA.md, DIVERGENCES.md, TONG_KET.md (tệp này)

---

# PHỤ LỤC v3.4 — Kiểm toán trễ E2E sâu & tối ưu gõ dấu (30-08-2026)

**Nhiệm vụ:** đo trễ đầu-cuối cho từng phím dấu (sắc/huyền/hỏi/ngã/nặng, d→đ,
ường/ươ…), sửa theo kết quả đo, không phá độ chính xác, đóng gói v3.4.
Báo cáo đầy đủ: **LATENCY_AUDIT_REPORT.md** · Giao thức xác minh máy thật:
**VERIFICATION_PROTOCOL.md** · Bench mới: `tests/bench_tone_latency.cpp`.

## Phát hiện trung tâm của cuộc kiểm toán

Khối lượng đo E2E đóng băng của v3.3.1 thực chất là **99,9% phím-vượt-qua**
(đo thực tế: 108 lần chỉnh sửa / 100.000 phím) — tức con số "p50 1,055 µs"
chủ yếu đo đường passthrough, không phải đường gõ dấu mà người dùng than
"bộ gõ lag khi gõ dấu". v3.4 bổ sung bench theo **quần thể dấu** (8 họ phím ×
3 đường output × 2 chế độ rào chặn × 3 mức chi phí TSF 0/100µs/1ms), dùng
đúng mã thật: TextEngine + InlineEmitter + ring Vyukov 1024/4096 + rào chặn.

## S1 — rào chặn thứ tự trên hook-thread (cơn ác mộng thật sự đo được)

| E2E phím dấu, TSF ~1 ms (Word) | p99 TRƯỚC (spin 2 ms) | p99 SAU (hybrid ≤ 1 ms) |
|---|---|---|
| tone-append (as → á) | 23.450 µs | **1.012 µs** |
| dbar (dd → đ) | 22.623 µs | **1.009 µs** |
| reposition (asa → ấ) | 24.619 µs | **2.003 µs** |
| restore-reissue (asas) | 26.295 µs | **2.002 µs** |
| double-tone (oasas) | 24.880 µs | **2.005 µs** |
| mid-word (ddawjng → đặng) | 26.232 µs | **2.002 µs** |
| horn-compound (uows → ướ) | 2.011 µs | **2.004 µs** |
| heavy-word (dduowcj → được) | 26.091 µs | **1.027 µs** |

Spin cũ **tự làm ra độ trễ nó chờ đợi** (chiếm nhân của consumer trên máy ít
nhân) — một phiên TSF 1 ms phình thành p99 ~23–26 ms. `EditDrainBarrier` mới:
spin ~2 µs rồi **chờ sự kiện** "đã hút cạn" do consumer phát đúng lúc
`pendingEdits` về 0, trần cứng **1 ms** (có unit test khẳng định trần).
p50 giữ ≈ chi phí commit — đó là *đúng tính năng* (phím sau không được vượt
trước chỉnh sửa), cái được sửa là **đuôi**.

## Các sửa còn lại (đều có số đo trong LATENCY_AUDIT_REPORT.md)

- **S4**: giao thức thức-tỉnh parked-flag — chỉ SetEvent khi consumer thật sự
  đỗ; bỏ 1 lần gọi nhân cho mỗi phím lúc đang đánh dồn dập.
- **S3**: `commitOne` (1 phiên TSF, không cấp phát cho phím dấu đơn) +
  cache `ITfInsertAtSelection` theo ngữ cảnh (bỏ 1 cặp QI/Release mỗi delta).
  `TF_ES_ASYNCDONTCARE` bị **loại** có lý do: trả về trước khi chữ vào văn bản
  sẽ phá rào chặn thứ tự (lỗi "ma" quay lại).
- **S2**: giữ SendInput ngay trong hook làm mặc định (mạch product-side toàn
  phần 0,16–0,44 µs p50 qua shim; đường deferred tốn thêm 1,2–1,5 µs mỗi phím
  do phải qua ring + wake). Vẫn ship chế độ **deferred chọn-không**
  (`OPENKEY_INLINE_MODE=deferred`) để đo A/B trên máy thật.
- **S5**: engine từng họ dấu ~91–133 ns/phím (bench 2 tầng) — không họ nào
  thoái bộ; không phải sửa engine.
- **S6**: nhịp tim watchdog bỏ syscall GetLastInputInfo khi con dấu của mình
  còn tươi (~30 ns khi đang gõ); hành vi phát hiện không đổi.

## Chính xác — không thoái bộ

Mega differential đầy đủ sau mỗi đợt sửa: **0 sai khác văn bản** (tất cả các
suite, gồm so khớp 3-phía với engine 2.0.5 thật). ctest 3/3 PASS (thêm 5 test
rào chặn). Cảnh báo biên dịch: 0.

## Ghi nhận thẳng thắn (hạn chế của môi trường)

Sandbox Linux 2 vCPU không chạy được Windows: mọi con số là của shim +
MinGW cross-build; tiếng _noise_ của host cho p50 ~10 µs và ~3% cú giật
>100 µs trên bench E2E (trùng ghi nhận đóng băng của v3.3.1). Số cần **máy
thật Windows** (SendInput thật, TSF thật trong Word/Chrome, ETW/WPA) theo
VERIFICATION_PROTOCOL.md §3–§6 — đã soạn sẵn từng bước để người dùng tự chạy.
