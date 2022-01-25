for folder in scenes/*
do
    for scene in "$folder"/*
    do
        scene_name="$(basename -- $scene)"
        ./bin/dgram render --scene "${scene}/${scene_name}.json" --output out/${scene//\//__}.png --resolution 1280
    done
done