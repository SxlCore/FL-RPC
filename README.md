# FL Studio Discord RPC

Shows in Discord:
  1. Playing FL Studio
  2. Working on name.flp
  3. Total time spent on project - %h %m
  4. Github button
  
## How to build

1. Open this project in your VS
2. Build it normally as default project (no dependencies required)
3. Done!

## How to install

Copy version.dll file from "../build/Release/" into FL Studio (next to FL64.exe):

Start FL Studio normally. RPC appears in Discord automatically (even if you have changed project).

## Discord Dev Portal (custom ur rpc pics or name)
To display your fl icon instead of mine, you should go here:
1. https://discord.com/developers/applications -> Create ur own application -> Set application name and image as you wish -> Then just save it
2. After that, go to Rich Presence -> Art Assets -> and upload image with key: fl_studio
3. And the last thing, you need to copy your Application ID from General settings and paste it to the project in "APP_ID = " field.
