<p align="right">
  <a href="host-tests-on-windows.md">English</a> · <strong>简体中文</strong>
</p>

# Windows 上的主机测试

[`tools/validate.sh`](../../../tools/validate.sh) 是仓库的主机测试门禁。在 Windows 主机上
门禁可以完整跑完,每个套件都通过,另有 21 个测试方法被跳过:它们都需要目录符号链接,而
Windows 在未开启开发者模式或未提权时拒绝创建。本文记录这一主机特有的行为和它对 CI 的
意义。

## 环境与复现

以下结果产自:

| 项目 | 取值 |
| --- | --- |
| 主机 | Windows 11 (10.0.26300),Git Bash / MINGW64 |
| 编译器 | llvm-mingw clang 22.1.8(`x86_64-w64-windows-gnu`),作为 `cc` 和 `gcc` 使用 |
| Python | 3.13.14 |
| ESP-IDF | v5.5.3 |

静态门禁在该主机上可无人值守地运行:

```console
$ ./tools/validate.sh --static
Repository checks: PASS (300 text files scanned)
...
Host tests: PASS
```

固件门禁需要已激活的 ESP-IDF 5.5.3 环境,不在本文结果之内。在原生 Windows 上,这指的是
Espressif 安装器创建的 ESP-IDF PowerShell 或命令提示符,而不是 Git Bash,因为 ESP-IDF 拒绝
MSys/Mingw shell;参见[环境搭建](environment-setup.zh_CN.md#windows-与-wsl2)。

单跑 C 套件时使用与门禁相同的命令形式。例如:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_alert.c main/alert.c main/usage_parse.c -o /tmp/test_alert
```

## 跳过了什么,以及为什么:目录符号链接

三个套件会探测主机能否创建目录符号链接,并在收到 `WinError 1314`("客户端没有所需的特权")
时调用 `skipTest`:

| 套件 | 跳过数 | 被跳过用例断言的内容 |
| --- | --- | --- |
| `test_check_repo` | 4 | 符号链接形式的 `VENDORED_DOC_ROOTS` 条目,以及指向一方代码树的 vendored 符号链接,仍然受检 |
| `test_archive_firmware` | 5 | 符号链接的源文件、归档根目录和已打包文件会被拒绝 |
| `test_install_passport_skills` | 12 | 安装器创建相对链接、保留外部条目、报告部分失败 |

该主机没有这一权限,因此上述 21 个用例全部跳过。开启开发者模式或以提权方式运行应当可以
清除它们;这一预期本文未经验证,因为它属于主机层面的改动,而不是仓库能自行完成的事。

技能安装器会报告这一限制而不是静默失败:它在第一个链接处停止,保留已创建的内容,并打印
可供直接读取的权威 `skills/passport-*/SKILL.md` 路径。

同一权限限制也解释了为什么某项仓库检查会报告 `destination already exists and is not a
symlink`。在该主机上,已有的 `.agents/skills/passport-*` 条目是目录 junction:
`os.readlink()` 和 `os.path.isjunction()` 能识别它们,但 `stat.S_ISLNK()` 不能,于是
`inspect_installation()` 把它们判为冲突。

## 与 CI 的关系

`.github/workflows/` 在 `ubuntu-latest` 上运行 `./tools/validate.sh --static` 和
`./tools/validate.sh --firmware`,在那里创建符号链接不需要特权,所以此处跳过的 21 个用例
在那边会真正执行。这些跳过属于 Windows 主机现象,并不说明 CI 会变红。
