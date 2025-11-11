# Video to .ass subtitles
This project aims to let you watch videos the way they're meant to be watched: in the subtitles. It transforms a mkv video into a .ass subtitle file that you can merge with a video using ffmpeg to then have your original video as an overlay (of subtitles) to your second video.

***WARNING***: this project serves no purpose. There is no reason for which anyone would spend time on such a project.

## The way subtitles work:
Subtitles today often use the ".ass" file format. It's a format that lets you create an insane variety of effects with simple text overlayed above a video and this format can be viewed with a lot of media viewers like VLC or mpv. This program imports a mkv video and reads the pixel values and outputs a "output.ass" file which includes the video under subtitle form. You can then merge this file with a video file using ffmpeg or any other tool. 

## How to use it:
Simply compile and run the "read_file.cpp" file and then run the executable with a video file name as parameter and it will output a "output.ass" file.
You can run the executable like this: `./read_file input.mkv`. 

You can then merge the subtitles with another video like this (with ffmpeg):
`ffmpeg -i background_video.mkv -i output.ass -c copy -c:s ass -disposition:s:0 default -y output.mkv`

This will create a video with the correct subtitles. The special subtitles will be off by default but can be turned on with the software used for viewing it. For the subtitles to be on by default, you can run `ffmpeg -i background_video.mp4 -i output.ass -c copy -c:s ass -disposition:s:0 default output.mkv` instead.


Edit the `pixel_block_size` to change the resolution of the subtitle output and edit the  `frame_process_interval`  to change the fps of the subtitle video. A larger `pixel_block_size` will lead to larger subtitle-pixels and so a lower resolution. If vertical banding starts appearing when editing the `pixel_block_size`, you can change (increase) the value of what gets substract from `pixel_block_size` in the for loop on line 301. 

Some viewers work better than others with the subtitles. For example, mpv is much much better than vlc at rendering all of the subtitles. 

