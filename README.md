# FL Studio Discord RPC

Shows in Discord:
  Playing FL Studio
  Working on name.flp
  Total time spent on project - %h %m
  Github button
  
## How to build

1. Open this project in your VS
2. Build it normally as default project (no dependencies required)
3. Done!

## How to install

Copy version.dll file from "../build/Release/" into FL Studio (next to FL64.exe):

Start FL Studio normally. RPC appears in Discord automatically (even if you have changed project).

## Discord Dev Portal (custom ur rpc pics or name)

To display your fl icon instead of mine, you should go here:
https://discord.com/developers/applications -> Create ur own application -> Set application name and image as you wish -> Then just save it
After that, go to Rich Presence -> Art Assets -> and upload image with key: fl_studio
And the last thing, you need to copy your Application ID from General settings and paste it to the project in "APP_ID = " field.
