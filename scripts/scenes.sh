for folder in scenes/*
do
    for file in "$folder"/*
    do
        ./bin/dgram render --scene "$file" --output out/${file//\//__}.png --resolution 1280
    done
done