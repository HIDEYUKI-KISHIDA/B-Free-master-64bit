#!/usr/bin/env python3
"""Patch BusyBox bb_pwd.c so uid/gid 0 map to root without musl getpwuid."""
import pathlib
import sys

p = pathlib.Path(sys.argv[1])
t = p.read_text()
if "B-Free: root uid0" in t:
    print("[busybox] bb_pwd.c already patched")
    raise SystemExit(0)

replacements = [
    (
        """char* FAST_FUNC xuid2uname(uid_t uid)
{
	/* Note: used in nofork applets (whoami), be careful not to leak anything */
	struct passwd *pw = xgetpwuid(uid);
	return pw->pw_name;
}""",
        """char* FAST_FUNC xuid2uname(uid_t uid)
{
	/* B-Free: root uid0 — avoid musl getpwuid crash in guest */
	if (uid == 0)
		return (char *)"root";
	/* Note: used in nofork applets (whoami), be careful not to leak anything */
	struct passwd *pw = xgetpwuid(uid);
	return pw->pw_name;
}""",
    ),
    (
        """char* FAST_FUNC uid2uname(uid_t uid)
{
	struct passwd *pw = getpwuid(uid);
	return (pw) ? pw->pw_name : NULL;
}""",
        """char* FAST_FUNC uid2uname(uid_t uid)
{
	/* B-Free: root uid0 */
	if (uid == 0)
		return (char *)"root";
	struct passwd *pw = getpwuid(uid);
	return (pw) ? pw->pw_name : NULL;
}""",
    ),
    (
        """char* FAST_FUNC xgid2group(gid_t gid)
{
	struct group *gr = xgetgrgid(gid);
	return gr->gr_name;
}""",
        """char* FAST_FUNC xgid2group(gid_t gid)
{
	/* B-Free: root gid0 */
	if (gid == 0)
		return (char *)"root";
	struct group *gr = xgetgrgid(gid);
	return gr->gr_name;
}""",
    ),
    (
        """char* FAST_FUNC gid2group(gid_t gid)
{
	struct group *gr = getgrgid(gid);
	return (gr) ? gr->gr_name : NULL;
}""",
        """char* FAST_FUNC gid2group(gid_t gid)
{
	/* B-Free: root gid0 */
	if (gid == 0)
		return (char *)"root";
	struct group *gr = getgrgid(gid);
	return (gr) ? gr->gr_name : NULL;
}""",
    ),
]

for old, new in replacements:
    if old not in t:
        sys.stderr.write("pattern missing in %s\n" % p)
        sys.stderr.write("---\n%s\n---\n" % old[:120])
        raise SystemExit(1)
    t = t.replace(old, new, 1)

p.write_text(t)
print("[busybox] patched bb_pwd.c for root uid/gid 0")
