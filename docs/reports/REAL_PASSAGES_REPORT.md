# Real mixed English–Vietnamese passages — results

Every passage is typed key-by-key with the Telex input method ACTIVE (the realistic
'user types both languages' case), through the KieeKey engine, the clean-room oracle,
and the vendored OpenKey 2.0.5 engine. Consumer semantics identical to the main benchmark.

| # | kind | passage | eng==ora | eng==2.0.5 | eng==intended |
|---|---|---|---|---|---|
| 1 | VN | Xin chào, tôi tên là Nam. Rất vui được gặp bạn! | ✓ | ✓ | ✓ |
| 2 | VN | Hôm nay trời đẹp quá, chúng ta đi chơi nhé! | ✓ | ✓ | ✓ |
| 3 | VN | Tôi đang học tiếng Việt ở Cần Thơ | ✓ | ✓ | ✓ |
| 4 | VN | Cảm ơn bạn đã giúp đỡ tôi rất nhiều! | ✓ | ✓ | ✓ |
| 5 | VN | Bạn có khỏe không? Tôi khỏe, cảm ơn! | ✓ | ✓ | ✓ |
| 6 | VN | Công việc hôm nay nhiều quá, tôi phải làm thêm giờ | ✓ | ✓ | ✓ |
| 7 | VN | Món ăn này ngon quá, bạn nấu giỏi thật đấy! | ✓ | ✓ | ✓ |
| 8 | VN | Ngày mai chúng ta họp lúc 9 giờ sáng nhé | ✓ | ✓ | ✓ |
| 9 | VN | Tôi sẽ gửi báo cáo cho bạn trước cuối tuần | ✓ | ✓ | ✓ |
| 10 | VN | Em yêu anh nhiều lắm, anh có nhớ em không? | ✓ | ✓ | ✓ |
| 11 | MIX | Please check the file tôi đã gửi qua email nhé | ✓ | ✓ | ✓ |
| 12 | MIX | OK, tôi sẽ confirm lại với team rồi báo bạn sau | ✓ | ✓ | ✗ |
| 13 | MIX | Meeting bị hủy rồi, hẹn gặp lại vào tuần sau nha | ✓ | ✓ | ✗ |
| 14 | MIX | Facebook của mình bị hack rồi, đổi password gấp nha | ✓ | ✓ | ✗ |
| 15 | MIX | Báo cáo này cần submit trước 5 giờ chiều | ✓ | ✓ | ✓ |
| 16 | MIX | Anh ơi, em gửi file qua WeChat được không? | ✓ | ✓ | ✗ |
| 17 | MIX | Công ty Google tuyển dụng nhiều vị trí mới lắm | ✓ | ✓ | ✗ |
| 18 | MIX | Tôi vừa xem video trên YouTube rất hay | ✓ | ✓ | ✓ |
| 19 | MIX | Windows update xong rồi, máy chạy nhanh hơn hẳn | ✓ | ✓ | ✗ |
| 20 | MIX | Số điện thoại của tôi là 0901234567, gọi cho tôi nhé | ✓ | ✓ | ✓ |
| 21 | NUM | Giá sản phẩm là 1.250.000 đồng, chưa gồm thuế | ✓ | ✓ | ✓ |
| 22 | NUM | Hẹn gặp bạn lúc 14:30 ngày 15/08/2026 nhé | ✓ | ✓ | ✓ |
| 23 | NUM | Kết quả kiểm tra: 95/100 điểm, rất tốt! | ✓ | ✓ | ✓ |
| 24 | NUM | Tôi sinh ngày 20/10/1995 ở Hà Nội | ✓ | ✓ | ✓ |
| 25 | EN | Please send me the file as soon as possible | ✓ | ✓ | ✗ |
| 26 | EN | Thank you for your help, see you tomorrow | ✓ | ✓ | ✗ |
| 27 | EN | The meeting is cancelled, we will reschedule | ✓ | ✓ | ✗ |
| 28 | EN | I love this city so much, it is beautiful | ✓ | ✓ | ✗ |
| 29 | CHAT | Uhm, để mình check lại đã nha | ✓ | ✓ | ✓ |
| 30 | CHAT | Ok ok, tối nay đi ăn gì? | ✓ | ✓ | ✓ |
| 31 | CHAT | Hehe, bạn giỏi thật đó! | ✓ | ✓ | ✓ |
| 32 | CHAT | Alo alo, có ai ở nhà không? | ✓ | ✓ | ✓ |

## Totals

- engine == oracle: 32/32
- engine == 2.0.5 (3-way agreement): 32/32
- engine == intended: 22/32
- pure-Vietnamese round-trip: 10/10

## Where the intended text differs

One cause remains. Passages 12–14, 16–17, 19, 25–28 are the DOCUMENTED real-IME
behavior for English words typed with the Telex IME on (engine == oracle == 2.0.5, so
KieeKey reproduces the legacy engine byte-for-byte). Passage 5 was the user-reported
shifted-symbol data loss; it is **fixed** (see below) and now matches 2.0.5.

| # | intended (what the user wanted) | actual output (engine) | 2.0.5 agrees? | cause |
|---|---|---|---|---|
| 12 | OK, tôi sẽ confirm lại với team rồi báo bạn sau | OK, tôi sẽ confirmlại với team rồi báo bạn sau | ✓ | shared real-IME behavior (engine==2.0.5) |
| 13 | Meeting bị hủy rồi, hẹn gặp lại vào tuần sau nha | Meetingbị hủy rồi, hẹn gặp lại vào tuần sau nha | ✓ | shared real-IME behavior (engine==2.0.5) |
| 14 | Facebook của mình bị hack rồi, đổi password gấp nha | Facebook của mình bị hack rồi, đổi pasword gấp nha | ✓ | shared real-IME behavior (engine==2.0.5) |
| 16 | Anh ơi, em gửi file qua WeChat được không? | Anh ơi, em gửi file qua WeChatđược không? | ✓ | shared real-IME behavior (engine==2.0.5) |
| 17 | Công ty Google tuyển dụng nhiều vị trí mới lắm | Công ty Googletuyển dụng nhiều vị trí mới lắm | ✓ | shared real-IME behavior (engine==2.0.5) |
| 19 | Windows update xong rồi, máy chạy nhanh hơn hẳn | Windowsupdate xong rồi, máy chạy nhanh hơn hẳn | ✓ | shared real-IME behavior (engine==2.0.5) |
| 25 | Please send me the file as soon as possible | Please send me the file á sôn á posible | ✓ | shared real-IME behavior (engine==2.0.5) |
| 26 | Thank you for your help, see you tomorrow | Thank you for yourhelp, sê you tômrow | ✓ | shared real-IME behavior (engine==2.0.5) |
| 27 | The meeting is cancelled, we will reschedule | The meetingí cancelled, wewillrếchdule | ✓ | shared real-IME behavior (engine==2.0.5) |
| 28 | I love this city so much, it is beautiful | I love thí city so much, it í beautiful | ✓ | shared real-IME behavior (engine==2.0.5) |

### Shared real-IME behavior (passages with English words, IME on)

Telex tone keys hit English words (`see`→`sê`, `is`→`í`, `w`→`ư`), and the
wrong-spelling restore then reverts the word on Space and consumes the Space key
(win32 OpenKey.cpp returns -1 on a consumed key), gluing the next word
(`confirm` → `confirmlại`). This is the documented real-IME behavior — the
user should toggle the IME off for pure-English segments. KieeKey reproduces it
byte-for-byte (engine == 2.0.5).

### Engine defect (passage 5) — FIXED

`Bạn có khỏe không? Tôi khỏe, cảm ơn!`: the `?` typed with no space after the composed
word `không`, followed by a Space, used to make the engine revert `không` to `khoong`
and delete the `?` (and consume the Space, gluing `Tôi`). The fix — classifying the 21
shifted symbols as word breaks in `TextEngine::isWordBreakChar` (oracle updated in
lockstep) — resolves it: this passage now matches 2.0.5 and the intended text.
Full 21-symbol matrix and 38 dirty passages: `DIRTY_INPUT_REPORT.md` (permanent
regression suite, 0 ENGINE-DEFECT).
