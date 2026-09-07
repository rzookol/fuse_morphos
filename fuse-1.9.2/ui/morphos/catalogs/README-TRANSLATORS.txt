Fuse MorphOS catalogs
=====================

Catalogs/Fuse.cd is the common CatComp description file.
Each translation has its own directory below Catalogs/ and its own Fuse.ct.

Example:
  Catalogs/Fuse.cd
  Catalogs/polski/Fuse.ct
  Catalogs/polski/Fuse.catalog

To add another language, create Catalogs/<language>/Fuse.ct from Fuse.cd,
translate the strings, set the correct ## language and ## codeset fields,
and build Fuse.catalog with MorphOS CatComp.

The Polish Fuse.ct is encoded as ISO-8859-2 (MIBenum/codeset 5).
Do not convert it to UTF-8 unless the catalog codeset and MorphOS runtime
expectations are changed accordingly.
