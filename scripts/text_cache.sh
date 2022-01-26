for folder in scenes/*
do
    for scene in "$folder"/*
    do
        scene_name="$(basename -- $scene)"
        ./bin/dgram render_text --scene "${scene}/${scene_name}.json" --resolution 1280
    done
done