CHÀO — CHỈ 3 VIỆC THÔI
======================

VIEC 1 (30 giây) — CHUẨN BỊ:
  * Mở KieeKey lên (nếu chưa chạy — icon trong khay hệ thống, cạnh đồng hồ).
  * ĐÓNG cửa sổ "Cài đặt" của KieeKey (nếu đang mở).
  * Chụp 1-2 ảnh bằng điện thoại: ảnh TOÀN BỘ cửa sổ Cài đặt (kể cả thanh
    tiêu đề trên cùng), và ảnh tab "Cấp độ" nếu bạn vào được.

VIEC 2 (bấm 1 lần) — CHẠY:
  * ĐÚP CHUỘT VÀO FILE:  run_probe_and_report.bat
  * Nếu Windows hiện màn xanh "Windows protected your PC":
        bấm "More info"  rồi bấm "Run anyway".
  * Một cửa sổ đen hiện ra, sau đó cửa sổ "Cài đặt" của KieeKey sẽ TỰ MỞ,
    TỰ chuyển qua lại các tab, TỰ kéo thanh cuộn.
  *   >>> ĐỪNG CHẠM VÀO. ĐỪNG MỞ CỬA SỔ KHÁC ĐÈ LÊN.   <<<
  * Chờ khoảng 3-5 phút. Xong thì cửa sổ đen hiện dòng "DONE." —
    bấm TÙY Ý phím nào đó để đóng.

VIEC 3 (gửi về) — GỬI:
  * Trong thư mục này sẽ có file MỚI:  kieekey-report-<ngày-giờ>.zip
  * Gửi vào chat 1 LÚC CẢ:  file zip đó  +  1-2 ảnh chụp của việc 1.
  * Hết. Không cần làm gì thêm.

NẾU GẶP LỖI Ở BẤT KỲ BƯỚC NÀO:
  * ĐỪNG XÓA gì, đừng panic.
  * Chụp ảnh HOẶC copy nguyên văn chữ trong cửa sổ đen (cả dòng màu đỏ)
    rồi gửi vào chat. Mình sẽ nhìn chính xác chỗ nào và chỉ đúng bước.

File nào làm gì (cho người thích biết — KHÔNG cần đọc cũng chạy được):
  * run_probe_and_report.bat  : file duy nhất cần bấm đúp
  * collect_report.ps1        : script thu thập thông tin (bat gọi nó)
  * kieekey_ui_probe.exe      : chương trình đo cửa sổ Cài đặt thật
  * kieekey_ui_probe.pdb      : file kỹ thuật, để chung là được
  * BUOC_CHAY_PROBE_chi_tiet.txt : hướng dẫn chi tiết hơn (nếu cần)

Lưu ý an toàn: các file này CHỈ MỞ cửa sổ Cài đặt của KieeKey để đo và ghi
lại số liệu vào file — chúng KHÔNG cài hook bàn phím, KHÔNG tạo tray,
KHÔNG sửa settings, KHÔNG gửi gì lên internet.
