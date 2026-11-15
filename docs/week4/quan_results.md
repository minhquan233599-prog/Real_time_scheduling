# Kết quả kiểm tra phần Quân — Tuần 4

## Phạm vi

Kiểm tra độc lập phần nền của Quân, chưa tích hợp vòng đời
job RT của Dũng và trace của Thành.

Thay đổi trong kernel/proc.h, kernel/proc.c và kernel/defs.h:
- Mở rộng metadata RT.
- Thêm helper ràng buộc process về CPU 0.
- Phân loại process đủ điều kiện chạy.
- Chọn EDF theo deadline tuyệt đối, hòa thì chọn PID nhỏ hơn.
- Luân phiên process thường bằng cursor.

## Kết quả

| Kiểm tra | CPUS=1 | CPUS=4 |
|---|---|---|
| rttest | 6/6 PASS | 6/6 PASS |
| rtstress | 10/10 PASS | 10/10 PASS |
| usertests | ALL TESTS PASSED | ALL TESTS PASSED |
| ls và echo sau suite | Hoạt động | Hoạt động |

Build kernel và fs.img: BUILD_EXIT_CODE=0.

## Minh chứng

- logs/quan-build.log
- logs/quan-version.txt
- logs/quan-changes.patch
- logs/quan-qemu-cpus1.log
- logs/quan-qemu-cpus4.log

## Giới hạn

Các kết quả trên kiểm tra khả năng build, chạy và tương thích.
Chưa xác minh lựa chọn EDF khi job active, release, budget,
completion, deadline miss hoặc ràng buộc CPU của workload RT.
