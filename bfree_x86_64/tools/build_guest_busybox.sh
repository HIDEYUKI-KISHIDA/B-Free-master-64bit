#!/usr/bin/env bash
# Build static x86_64 Linux/musl busybox for B-Free guest (exec_initrd busybox.elf).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BB_VER="${BFREE_BUSYBOX_VERSION:-1.36.1}"
if [[ -n "${BFREE_BUSYBOX_SRC:-}" ]]; then
  BB_SRC="$BFREE_BUSYBOX_SRC"
elif [[ "$(id -u)" -eq 0 ]] || [[ -w /root/src ]] 2>/dev/null; then
  BB_SRC="/root/src/busybox"
else
  BB_SRC="$ROOT/.cache/busybox-src"
fi
BB_EXTRACT="${BFREE_BUSYBOX_EXTRACT:-$(dirname "$BB_SRC")/busybox-${BB_VER}}"
OUT="$ROOT/userland/busybox_guest/busybox.elf"
OUT_DIR="$(dirname "$OUT")"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

mkdir -p "$OUT_DIR"

if [[ ! -f "$BB_SRC/Makefile" ]]; then
  mkdir -p "$(dirname "$BB_SRC")"
  tmp="$ROOT/.cache/busybox-${BB_VER}.tar.bz2"
  mkdir -p "$ROOT/.cache"
  if [[ ! -f "$tmp" ]]; then
    echo "[busybox] download busybox-${BB_VER}.tar.bz2"
    curl -fsSL "https://busybox.net/downloads/busybox-${BB_VER}.tar.bz2" -o "$tmp"
  fi
  rm -rf "$BB_EXTRACT"
  tar -xjf "$tmp" -C "$(dirname "$BB_SRC")"
  rm -rf "$BB_SRC"
  mv "$BB_EXTRACT" "$BB_SRC"
fi

if ! command -v musl-gcc >/dev/null 2>&1; then
  echo "[busybox] installing musl-tools (needs root; try: sudo apt install musl-tools)"
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "[busybox] ERROR: musl-gcc not found and not running as root" >&2
    exit 1
  fi
  apt-get update -qq
  apt-get install -y musl-tools curl ca-certificates
fi

bb_cfg_y() {
  local key="$1"
  sed -i "/^${key}=/d;/^# ${key} is not set/d" .config
  echo "${key}=y" >> .config
}

bb_cfg_n() {
  local key="$1"
  sed -i "/^${key}=/d;/^# ${key} is not set/d" .config
  echo "# ${key} is not set" >> .config
}

bb_cfg_val() {
  local key="$1"
  local val="$2"
  sed -i "/^${key}=/d;/^# ${key} is not set/d" .config
  echo "${key}=${val}" >> .config
}

apply_bfree_busybox_config() {
  bb_cfg_y CONFIG_STATIC
  bb_cfg_y CONFIG_NOMMU
  bb_cfg_y CONFIG_BUSYBOX
  bb_cfg_y CONFIG_ASH
  bb_cfg_n CONFIG_HUSH
  bb_cfg_y CONFIG_SH_IS_ASH
  bb_cfg_n CONFIG_SH_IS_HUSH
  bb_cfg_n CONFIG_SH_IS_NONE
  bb_cfg_n CONFIG_BASH_IS_ASH
  bb_cfg_n CONFIG_BASH_IS_HUSH
  bb_cfg_y CONFIG_BASH_IS_NONE
  bb_cfg_y CONFIG_FEATURE_SH_STANDALONE
  bb_cfg_y CONFIG_FEATURE_SH_NOFORK
  bb_cfg_y CONFIG_FEATURE_PREFER_APPLETS
  bb_cfg_val CONFIG_BUSYBOX_EXEC_PATH "/busybox.elf"
  # Network applets: enable CLI options/status display.
  # Without these, `ping -c ...` treats `-c` as HOST, and `ifconfig` refuses
  # status display.
  bb_cfg_y CONFIG_FEATURE_FANCY_PING
  bb_cfg_y CONFIG_FEATURE_IFCONFIG_STATUS
  bb_cfg_y CONFIG_FEATURE_SH_EXTRA_QUIET
  # $((arith)) is used by Phase B0 tests (and common shell scripts)
  bb_cfg_y CONFIG_FEATURE_SH_MATH
  # Guest serial: lineedit (raw tty + poll) corrupts stack after Enter; use kernel canonical read.
  bb_cfg_n CONFIG_FEATURE_EDITING
  bb_cfg_n CONFIG_FEATURE_EDITING_FANCY_PROMPT
  bb_cfg_n CONFIG_FEATURE_TAB_COMPLETION
  bb_cfg_y CONFIG_ASH_EXPAND_PRMT
  bb_cfg_n CONFIG_ASH_MAIL
  bb_cfg_y CONFIG_ASH_ECHO
  bb_cfg_y CONFIG_ASH_TEST
  bb_cfg_y CONFIG_ASH_SLEEP
  bb_cfg_y CONFIG_ASH_PRINTF
  bb_cfg_y CONFIG_ASH_JOB_CONTROL
  bb_cfg_n CONFIG_FEATURE_LS_SORTFILES
  bb_cfg_n CONFIG_FEATURE_LS_TIMESTAMPS
  bb_cfg_n CONFIG_FEATURE_LS_USERNAME
  bb_cfg_n CONFIG_FEATURE_LS_COLOR
  bb_cfg_n CONFIG_FEATURE_LS_WIDTH
  bb_cfg_n CONFIG_FEATURE_LS_RECURSIVE
  bb_cfg_y CONFIG_FEATURE_BUFFERS_USE_MALLOC
  bb_cfg_val CONFIG_FEATURE_COPYBUF_KB 4
  bb_cfg_val CONFIG_PASSWORD_MINLEN 6
  bb_cfg_y CONFIG_TRY_LOOP_CONFIGURE
  bb_cfg_n CONFIG_LOOP_CONFIGURE
  bb_cfg_n CONFIG_NO_LOOP_CONFIGURE
  for app in ECHO CAT LS UNAME TRUE FALSE PWD WC HEAD TAIL TEST SLEEP PRINTF \
               GREP EGREP FGREP SORT MKDIR RM RMDIR TOUCH CP MV CLEAR DIRNAME BASENAME \
               FIND SED TR CUT LN READLINK ID WHOAMI ENV NICE UNIQ DD MD5SUM \
               XARGS COMM EXPAND UNEXPAND FOLD TAC REV STAT FACTOR SEQ YES SHUF \
               NPROC UNLINK REALPATH MORE \
               VI AWK TAR GZIP GUNZIP BZIP2 BUNZIP2 CHMOD LESS DATE CMP DIFF EXPR DC \
               PATCH HEXDUMP OD STRINGS DOS2UNIX UNIX2DOS UNZIP XZ UNXZ BC CAL TIME \
               PS TOP DMESG FREE UPTIME KILL MOUNT \
               PING IFCONFIG NC ROUTE HOSTNAME \
               SYNC REBOOT HALT POWEROFF WHICH DF \
               FEATURE_TAR_CREATE FEATURE_TAR_AUTODETECT FEATURE_FAST_TOP \
               FEATURE_IPV6; do
    bb_cfg_y "CONFIG_${app}"
  done
  # stat: default output calls human_time->localtime which crashes guest musl;
  # -c FORMAT (e.g. %s) avoids that path entirely.
  bb_cfg_y CONFIG_FEATURE_STAT_FORMAT
  bb_cfg_y CONFIG_NC_110_COMPAT
  bb_cfg_y CONFIG_NC_SERVER
  bb_cfg_y CONFIG_NC_EXTRA
  bb_cfg_y CONFIG_SHOW_USAGE
  bb_cfg_y CONFIG_ASH_HELP
  # CONFIG_FILE / mount: enable when applet+kernel support exist
}

verify_bfree_busybox_config() {
  local sym
  for sym in CONFIG_STATIC CONFIG_BUSYBOX CONFIG_ASH CONFIG_SH_IS_ASH \
             CONFIG_FEATURE_SH_STANDALONE CONFIG_FEATURE_SH_NOFORK; do
    grep -q "^${sym}=y" .config || {
      echo "[busybox] ERROR: missing ${sym}=y in .config" >&2
      exit 1
    }
  done
  if grep -q '^CONFIG_FEATURE_EDITING=y' .config; then
    grep -q '^CONFIG_FEATURE_EDITING_MAX_LEN=1024' .config || {
      echo "[busybox] ERROR: CONFIG_FEATURE_EDITING_MAX_LEN=1024 required when editing enabled" >&2
      exit 1
    }
  fi
}

verify_bfree_busybox_binary() {
  local app
  file busybox | grep -q 'statically linked' || {
    echo "[busybox] ERROR: busybox is not static" >&2
    exit 1
  }
  for app in echo ls cat pwd mkdir rm ps free uptime whoami id kill df; do
    ./busybox --list | grep -qx "$app" || {
      echo "[busybox] ERROR: applet '$app' missing from --list" >&2
      ./busybox --list | head -40
      exit 1
    }
  done
  ./busybox sh -c 'ls /' >/dev/null || {
    echo "[busybox] ERROR: './busybox sh -c ls /' failed on host" >&2
    exit 1
  }
  ./busybox sh -c 'echo hello | cat' | grep -qx hello || {
    echo "[busybox] ERROR: pipeline 'echo hello | cat' failed on host" >&2
    exit 1
  }
}

# Restore upstream NOFORK gate, then optionally widen to all applets (no musl fork TLS yet).
bfree_restore_ash_nofork_gate() {
  local ash="$BB_SRC/shell/ash.c"
  if grep -q 'B-Free: no fork syscall' "$ash"; then
    sed -i 's/if (applet_no >= 0) \/\* B-Free: no fork syscall \*\//if (applet_no >= 0 \&\& APPLET_IS_NOFORK(applet_no))/' "$ash"
  fi
  if grep -q 'B-Free: nofork all' "$ash"; then
    sed -i 's/if (applet_no >= 0) \/\* B-Free: nofork all \*\//if (applet_no >= 0 \&\& APPLET_IS_NOFORK(applet_no))/' "$ash"
  fi
  if grep -q 'B-Free: nofork except grep' "$ash"; then
    sed -i 's/if (applet_no >= 0 \&\& applet_no != 31 \&\& applet_no != 22 \&\& applet_no != 28) \/\* B-Free: nofork except grep \*\//if (applet_no >= 0 \&\& APPLET_IS_NOFORK(applet_no))/' "$ash"
    sed -i 's/if (applet_no >= 0 \&\& applet_no != APPLET_NO_grep \&\& applet_no != APPLET_NO_egrep \&\& applet_no != APPLET_NO_fgrep) \/\* B-Free: nofork except grep \*\//if (applet_no >= 0 \&\& APPLET_IS_NOFORK(applet_no))/' "$ash"
  fi
}

# Default: real vfork+execve (Phase 5). Set BFREE_NOFORK_ALL=1 to force
# in-process applets (legacy / faster smoke when debugging).
bfree_patch_ash_vfork_support() {
  local ash="$BB_SRC/shell/ash.c"
  local nofork_all="${BFREE_NOFORK_ALL:-0}"

  bfree_restore_ash_nofork_gate
  if [[ "$nofork_all" == "1" ]] && grep -q 'APPLET_IS_NOFORK(applet_no)' "$ash"; then
    sed -i 's/if (applet_no >= 0 && APPLET_IS_NOFORK(applet_no))/if (applet_no >= 0) \/\* B-Free: nofork all \*\//' "$ash"
  fi

  # FIX: vfork child MUST NOT use longjmp (ash_msg_and_raise) because the jmp_buf has the parent's stack pointer!
  python3 -c "import sys; data=open(sys.argv[1]).read(); open(sys.argv[1],'w').write(data.replace('ash_msg_and_raise(EXEND, \"%s: %s\", prog, errmsg(e, \"not found\"));', 'ash_msg(\"%s: %s\", prog, errmsg(e, \"not found\")); _exit(exerrno);'))" "$ash"

  # FIX: allow ASH to build with NOMMU
  sed -i 's/depends on !NOMMU//g' "$BB_SRC/shell/ash.c" "$BB_SRC/shell/Config.src" 2>/dev/null || true
  sed -i 's/if !NOMMU &&/if/g' "$BB_SRC/shell/ash.c" "$BB_SRC/shell/Config.src" 2>/dev/null || true
  sed -i 's/# error "Do not even bother, ash will not run on NOMMU machine"//g' "$BB_SRC/shell/ash.c" || true
  sed -i 's/pid = fork();/pid = vfork();/g' "$BB_SRC/shell/ash.c" || true

  # Shared-AS cooperative vfork: forkchild must not freejob() the parent's
  # job table (MMU ash assumes fork() CoW; freejob → parent resume #PF).
  if ! grep -q 'B-Free: skip freejob under shared-AS vfork' "$ash"; then
    python3 - "$ash" <<'PY'
import sys
path = sys.argv[1]
text = open(path, encoding="utf-8", errors="replace").read()
old = "\tfor (jp = curjob; jp; jp = jp->prev_job)\n\t\tfreejob(jp);\n}"
new = ("\t/* B-Free: skip freejob under shared-AS vfork — would destroy parent jobs */\n"
       "\tif (0) for (jp = curjob; jp; jp = jp->prev_job)\n"
       "\t\tfreejob(jp);\n}")
idx = text.rfind(old)
if idx < 0:
    old2 = old.replace("\t", "    ")
    idx = text.rfind(old2)
    if idx >= 0:
        old = old2
        new = new.replace("\t", "    ")
if idx < 0:
    sys.stderr.write("[busybox] ERROR: forkchild freejob loop not found\n")
    sys.exit(1)
text = text[:idx] + new + text[idx + len(old):]
open(path, "w", encoding="utf-8").write(text)
print("[busybox] patched forkchild freejob skip")
PY
  fi

  # FIX: Prevent vfork children from calling exit(), which corrupts parent's stdio streams via __stdio_exit!
  if ! grep -q bfree_safe_exit "$BB_SRC/libbb/appletlib.c"; then
    echo "void bfree_safe_exit(int s) { fflush(0); _exit(s); }" >> "$BB_SRC/libbb/appletlib.c"
  fi

  if [[ "$nofork_all" == "1" ]]; then
    if ! grep -q 'B-Free: nofork except grep\|B-Free: nofork all\|B-Free nofork' "$ash"; then
      echo "[busybox] ERROR: ash.c NOFORK gate patch missing" >&2
      exit 1
    fi
  else
    if ! grep -q 'APPLET_IS_NOFORK(applet_no)' "$ash"; then
      echo "[busybox] ERROR: upstream APPLET_IS_NOFORK gate missing" >&2
      exit 1
    fi
    echo "[busybox] NOFORK-all disabled (Phase 5 path: force execve for NOEXEC)"
    # Shared-AS vfork cannot safely run NOEXEC applets in-process (they mutate
    # the parent's musl heap). Skip tryexec's NOEXEC branch so applets execve
    # into a private child address space like regular applets.
    if ! grep -q 'B-Free: force execve for NOEXEC' "$ash"; then
      sed -i 's/if (APPLET_IS_NOEXEC(applet_no)) {/if (0 \&\& APPLET_IS_NOEXEC(applet_no)) { \/* B-Free: force execve for NOEXEC *\//' "$ash"
      grep -q 'B-Free: force execve for NOEXEC' "$ash" || {
        echo "[busybox] ERROR: NOEXEC→execve patch failed" >&2
        exit 1
      }
    fi
  fi

  # cat: with nofork-all, avoid needless reexec; with NOFORK-all=0, force APPLET (execve).
  if [[ "$nofork_all" == "1" ]]; then
    sed -i 's/APPLET(cat, BB_DIR_BIN, BB_SUID_DROP)/APPLET_NOEXEC(cat, cat, BB_DIR_BIN, BB_SUID_DROP, cat)/' "$BB_SRC/coreutils/cat.c"
  else
    sed -i 's/APPLET_NOEXEC(cat, cat, BB_DIR_BIN, BB_SUID_DROP, cat)/APPLET(cat, BB_DIR_BIN, BB_SUID_DROP)/' "$BB_SRC/coreutils/cat.c"
  fi
  if [[ "$nofork_all" == "1" ]]; then
    if ! grep -q 'B-Free: path-resolved applet nofork' "$ash"; then
      sed -i 's/\t\tint applet_no = (- cmdentry.u.index - 2);/\t\tint applet_no = (- cmdentry.u.index - 2);\n\t\tif (applet_no < 0)\n\t\t\tapplet_no = find_applet_by_name(argv[0]); \/* B-Free: path-resolved applet nofork *\//' "$ash"
    fi
  fi
  # Force PS/TOP/UPTIME to NOFORK (procfs stubs are lighter in-process)
  sed -i 's/APPLET_NOEXEC(ps,/APPLET_NOFORK(ps,/' "$BB_SRC/procps/ps.c"
  sed -i 's/APPLET_NOEXEC(minips,/APPLET_NOFORK(minips,/' "$BB_SRC/procps/ps.c"
  sed -i 's/IF_TOP(APPLET(top, BB_DIR_USR_BIN, BB_SUID_DROP))/IF_TOP(APPLET_NOFORK(top, top, BB_DIR_USR_BIN, BB_SUID_DROP, top))/' "$BB_SRC/procps/top.c"
  sed -i 's/APPLET_NOEXEC(uptime,/APPLET_NOFORK(uptime,/' "$BB_SRC/procps/uptime.c"
  # Remaining NOFORK applets still share stdin FILE — clear sticky EOF.
  if ! grep -q 'clearerr(stdin); /\* B-Free \*/' "$BB_SRC/libbb/vfork_daemon_rexec.c"; then
    sed -i 's/\t\tapplet_name = tmp_argv\[0\];/\t\tapplet_name = tmp_argv[0];\n\t\tclearerr(stdin); \/* B-Free *\//' "$BB_SRC/libbb/vfork_daemon_rexec.c"
    grep -q 'clearerr(stdin); /\* B-Free \*/' "$BB_SRC/libbb/vfork_daemon_rexec.c" || {
      echo "[busybox] ERROR: clearerr(stdin) patch failed" >&2
      exit 1
    }
  fi
  python3 "$ROOT/tools/bfree_patch_nofork_fresh_g.py" "$BB_SRC/libbb/vfork_daemon_rexec.c"
  python3 "$ROOT/tools/bfree_patch_bb_pwd_root.py" "$BB_SRC/libbb/bb_pwd.c"
  python3 "$ROOT/tools/bfree_patch_df_synthetic.py" "$BB_SRC/coreutils/df.c"
}

bfree_patch_ash_inproc_pipe() {
  python3 "$ROOT/tools/bfree_patch_ash_inproc_pipe.py" "$BB_SRC/shell/ash.c"
}

bfree_patch_ash_bg_inline() {
  python3 "$ROOT/tools/bfree_patch_ash_bg_inline.py" "$BB_SRC/shell/ash.c"
}

bfree_patch_grep_simple() {
  python3 "$ROOT/tools/bfree_patch_grep_simple.py" "$BB_SRC/findutils/grep.c"
}

bfree_patch_sed_simple() {
  python3 "$ROOT/tools/bfree_patch_sed_simple.py" "$BB_SRC/editors/sed.c"
}

bfree_patch_lineedit_null_state() {
  local f="$BB_SRC/libbb/lineedit.c"

  if [[ ! -f "$f" ]]; then
    return 0
  fi
  if grep -q 'state && state->sh_get_var' "$f"; then
    return 0
  fi
  sed -i 's/home = state->sh_get_var ? state->sh_get_var("HOME")/home = state \&\& state->sh_get_var ? state->sh_get_var("HOME")/' "$f"
  sed -i 's/cwd_buf = state->sh_get_var/cwd_buf = state \&\& state->sh_get_var/' "$f"
}

# Non-interactive kconfig sync
bfree_busybox_sync_config() {
  yes "" | make oldconfig
}

cd "$BB_SRC"
make distclean >/dev/null 2>&1 || true
yes n | make allnoconfig >/dev/null 2>&1
apply_bfree_busybox_config
verify_bfree_busybox_config
bfree_patch_ash_vfork_support
bfree_patch_ash_inproc_pipe
bfree_patch_ash_bg_inline
bfree_patch_grep_simple
bfree_patch_sed_simple
bfree_patch_lineedit_null_state
bfree_busybox_sync_config
sed -i 's/#undef ENABLE_NC_SERVER/#define ENABLE_NC_SERVER 1/' include/autoconf.h
bb_cfg_y CONFIG_NC_SERVER
bb_cfg_y CONFIG_NC_EXTRA
verify_bfree_busybox_config
make -j"$JOBS" CC="musl-gcc" EXTRA_CFLAGS="-Dfork=vfork -Dexit=bfree_safe_exit" LDFLAGS="-static -Wl,-z,norelro -Wl,-Ttext-segment=0x500000" </dev/null
verify_bfree_busybox_binary
cp -f busybox "$OUT"
chmod +x "$OUT"
file "$OUT"
ls -la "$OUT"
echo "[busybox] wrote $OUT"
