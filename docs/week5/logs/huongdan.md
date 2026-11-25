# Benchmark tuần 5 — thời gian đáp ứng EDF/RM

## 1. Đo gì?

Đo thời gian đáp ứng job, theo kernel tick:

`response = COMPLETE.tick - RELEASE.tick`

Thời gian này bao gồm thời gian chờ CPU và thực thi. Đây không phải số tick CPU chính xác, WCET, throughput hoặc thời gian chạy toàn chương trình. Không đổi sang ms nếu chưa xác nhận chu kỳ tick của bản kernel đang dùng. Không dùng thời gian từ lúc gọi `rt_job_done()` trở về vì syscall này chờ kỳ tiếp theo.

Mỗi lần chạy một policy tạo ba worker A/B/C, mỗi worker hoàn thành ba job. Một mẫu của một tác vụ là trung bình response của ba job trong lượt đó. Năm lượt tạo **năm mẫu độc lập về lượt chạy**, mỗi mẫu chứa ba job; không gọi 15 job là 15 lần lặp độc lập. Các lượt vẫn chịu điều kiện thực thi chung của máy, nên không mặc định độc lập thống kê.

Mỗi điểm dữ liệu là `(CPUS, SAFE/COMPARE, EDF/RM, A/B/C)`. Báo mean, độ lệch chuẩn mẫu (stdev), min, max và range trên năm giá trị trung bình theo lượt. CSV lưu cả response của từng job để kiểm tra lại.

## 2. Hai file dùng cho benchmark

| File trong gói | Đặt vào dự án | Thay đổi |
|---|---|---|
| `user/rt_compare.c` | `user/rt_compare.c` | Thêm `[runs]` tùy chọn, từ 1 đến 20. Hiệu chuẩn một lần trước cả đợt; lặp cặp EDF/RM trên cùng mảng blocks. Khôi phục policy sau mỗi cặp. Dừng sau cặp lỗi, không thay lượt lỗi bằng lượt khác. |
| `scripts/bench_rt.py` | `scripts/bench_rt.py` | Đọc log; kiểm tra đủ mẫu, trace, worker, miss và đầu vào cố định; tạo CSV và báo cáo Markdown. |

Bản C trong gói dựa trên bản hai-policy calibration đã sửa, có đếm deadline miss. Không dùng thay cho bản main cũ nếu chưa có `rt_calibrate.c` mới tương ứng. Phần đổi thêm nằm trong `main()`: nhận số lượt, vòng lặp benchmark và khôi phục policy khi workload range lỗi. `worker()`, `run_workload()`, trace CSV và hàm hiệu chuẩn không đổi.

**Không sửa kernel, Makefile, `rt_calibrate.c`, `rt_rmtest.c` hoặc checker.** Quy tắc Makefile liên kết `rt_compare.o` và `rt_calibrate.o` hiện có vẫn dùng được. Gói không chứa bản `rt_calibrate.c` mới vì không cần sửa nó cho benchmark.

Lệnh cũ vẫn chạy một cặp:

```text
rt_compare safe
rt_compare compare
```

Lệnh benchmark chạy năm cặp mỗi bộ:

```text
rt_compare safe 5
rt_compare compare 5
```

Không chạy thủ công `rt_compare safe` năm lần thay cho lệnh có `5`: mỗi lần gọi chương trình sẽ hiệu chuẩn lại, có thể làm blocks thay đổi.

## 3. Chuẩn bị và build — Ubuntu

Giải nén gói vào thư mục tạm, chép hai file đúng đường dẫn vào nhánh làm việc của bạn. Không cần thay cả dự án. Đọc diff trước khi commit; nếu Dũng cũng đang sửa hai file workload, thống nhất phiên bản trước khi merge.

Chạy tại thư mục gốc dự án, sau khi tắt QEMU:

```bash
mkdir -p docs/week5/logs
clang-format -i user/rt_compare.c
clang-format --dry-run --Werror user/rt_compare.c
python3 -m py_compile scripts/bench_rt.py
make kernel/kernel fs.img
```

Chỉ chạy bước tiếp nếu build thành công. Giữ cùng bản code và ảnh đĩa trong đợt đo. Không chạy đồng thời nhiều QEMU dùng `fs.img`. Chọn tên log mới nếu tên dưới đây đã có dữ liệu cần giữ.

## 4. CPUS=1

Trong Ubuntu:

```bash
script -q -e -c 'make qemu CPUS=1' \
  docs/week5/logs/thanh-benchmark-cpus1.log
```

Trong xv6, chạy từng lệnh và chờ xuất hiện lại `$`:

```text
rt_compare safe 5
rt_compare compare 5
```

Mỗi lệnh sẽ hiệu chuẩn EDF và RM trước, sau đó chạy năm cặp trên cùng workload. Có thể mất vài phút do chu kỳ job và thời gian hiệu chuẩn; không đóng QEMU khi chương trình hoặc CSV chưa in xong. Khi cả hai lệnh hoàn tất và đã hiện `$`, thoát bằng Ctrl+A, nhả tay, rồi X.

Mỗi bộ cần kết thúc bằng:

```text
BENCHMARK RESULT: set=safe passed=5 runs=5 PASS
BENCHMARK RESULT: set=compare passed=5 runs=5 PASS
```

Hai dòng trên nằm ở cuối từng lệnh tương ứng, không nhất thiết liền nhau.

Trong Ubuntu, kiểm tra chức năng và thứ tự bằng checker đã có:

```bash
set -o pipefail
python3 scripts/check_rt_order.py \
  docs/week5/logs/thanh-benchmark-cpus1.log \
  | tee docs/week5/logs/thanh-benchmark-check-cpus1.log
```

Sau đó tính benchmark:

```bash
python3 scripts/bench_rt.py \
  docs/week5/logs/thanh-benchmark-cpus1.log \
  --cpus 1 --out docs/week5/benchmark/cpus1
```

## 5. CPUS=4

Trong Ubuntu:

```bash
script -q -e -c 'make qemu CPUS=4' \
  docs/week5/logs/thanh-benchmark-cpus4.log
```

Trong xv6, chạy từng lệnh, đợi xong:

```text
rt_compare safe 5
rt_compare compare 5
```

Thoát QEMU sau khi cả hai lệnh hoàn tất. Trong Ubuntu:

```bash
set -o pipefail
python3 scripts/check_rt_order.py \
  docs/week5/logs/thanh-benchmark-cpus4.log \
  | tee docs/week5/logs/thanh-benchmark-check-cpus4.log

python3 scripts/bench_rt.py \
  docs/week5/logs/thanh-benchmark-cpus4.log \
  --cpus 4 --out docs/week5/benchmark/cpus4
```

Nếu checker FAIL vì thiếu tình huống so sánh hoặc sai thứ tự, giữ FAIL đó; không kết luận toàn suite PASS chỉ từ script thống kê. Hai script có trách nhiệm khác nhau.

## 6. Kết quả và minh chứng

Mỗi thư mục `docs/week5/benchmark/cpus1` và `cpus4` có:

- `samples.csv`: ba response job và trung bình của từng lượt/tác vụ.
- `summary.csv`: số mẫu, mean, stdev, min, max, range cho từng bộ/policy/tác vụ.
- `benchmark.md`: bảng số liệu và phạm vi diễn giải, dùng để đưa vào báo cáo.

`BENCHMARK DATA: PASS` xác nhận dữ liệu đạt các điều kiện thu thập của script: đủ ít nhất năm lượt mỗi bộ, CSV đầy đủ, đầu vào giữ nguyên, các marker thành công và không có MISS. Nó không đặt ngưỡng thuật toán nào phải nhanh hơn và không thay checker thứ tự.

Giữ hai log thô, hai log checker, các CSV, báo cáo và phiên bản code thực sự đã chạy. Các log final cũ vẫn chứng minh các bài kiểm tra trước; không ghi đè chúng bằng benchmark.

## 7. Giới hạn và cách đọc kết quả

- EDF và RM dùng cùng C/T/blocks trong từng bộ của một đợt. Script kiểm tra cả INPUT và input_hash qua mọi mẫu, không chỉ giữa một cặp.
- CPUS=1 và CPUS=4 được hiệu chuẩn riêng, nên có thể có blocks khác nhau. Nếu đầu vào khác, không dùng chênh lệch thời gian để tuyên bố cấu hình CPU nào nhanh hơn. So sánh EDF/RM trong cùng cấu hình và bộ có đầu vào cố định.
- Cùng policy có thể có thời gian đáp ứng khác giữa các lượt do lịch chạy và tốc độ thực thi. Báo cả độ phân tán, không chỉ mean.
- Job đầu tính từ RELEASE lúc đăng ký nên gồm thời gian chờ cổng worker. Báo cáo áp dụng cho cách khởi động hiện tại, không khẳng định taskset đồng bộ release tuyệt đối.
- Calibration không bảo đảm WCET. Workload có thể vẫn vượt budget ở điều kiện khác. Nếu bất kỳ lượt nào FAIL, dừng để phân tích và giữ log; không bỏ lượt lỗi khỏi mẫu thống kê.
- Việc thử lại tối đa ba cửa sổ calibration thuộc bước chuẩn bị đã thống nhất; không phải tự chạy lại toàn benchmark để tìm PASS.
- Script không chấp nhận log bị cắt, mất trace, trùng lượt, sai CPUS hoặc workload đổi giữa mẫu. Khi lỗi sẽ in `BENCHMARK DATA: FAIL` và không tạo báo cáo mới. Không dùng báo cáo cũ còn trong thư mục để thay kết quả FAIL mới.
- Trong QEMU CPUS=4, RT vẫn ghim CPU 0. Đây là phép đo trên hệ thống nhiều CPU với phần RT đơn CPU.

## 8. Kiểm tra đã thực hiện khi chuẩn bị gói

Đã kiểm tra cú pháp C/Python và 14 tình huống mô phỏng: phép tính thống kê, xuất báo cáo, loại trace lỗi/thiếu mẫu/đổi workload, năm cặp dùng cùng blocks, đối số số lượt sai và dừng đúng khi workload FAIL. Đây là kiểm tra trên máy host với dữ liệu mô phỏng; chưa chạy benchmark thực tế trên QEMU ở môi trường này. Không có số liệu hiệu năng giả lập được điền làm kết quả thực tế.