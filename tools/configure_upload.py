from os.path import join

Import("env")


project_dir = env.subst("$PROJECT_DIR")
python_exe = env.subst("$PYTHONEXE")
esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
wrapper_script = join(project_dir, "tools", "upload_via_flash_args.py")
esptool_script = join(esptool_dir, "esptool.py")
upload_port = env.subst("$UPLOAD_PORT")
partitions_csv = join(project_dir, "partitions.csv")

env.Replace(PARTITIONS_TABLE_CSV=partitions_csv)
env.BoardConfig().update("upload.maximum_size", 0x400000)

if not upload_port:
    env.AutodetectUploadPort()
    upload_port = env.subst("$UPLOAD_PORT")

env.Replace(
    UPLOADER=python_exe,
    UPLOADCMD=(
        '"{python}" "{wrapper}" '
        '--python "{python}" '
        '--esptool "{esptool}" '
        '--build-dir "$BUILD_DIR" '
        '--port "{port}" '
        '--baud "$UPLOAD_SPEED" '
        '--chip esp32s3'
    ).format(
        python=python_exe,
        wrapper=wrapper_script,
        esptool=esptool_script,
        port=upload_port,
    ),
)
