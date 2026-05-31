# Aurc
Fast Easy way to Install and Update Aur and Non-Aur Arch Linux Packages!

- This project is now under the wavly org as a rewritten project
EMAIL ON THE AUR PKGBUILD IS EXPIRED; MESSAGE ME ONLY BY MY PROFILE LINKS ANYONE ELSE IS AN IMPERSONATOR

<div align = center><img src="https://github.com/resslr/aurc/assets/122219240/218741a8-0faa-4693-9fa8-feeb5285bfa9"><br><br>

&ensp;[<kbd> <br> Usage <br> </kbd>](#Usage)&ensp;
&ensp;[<kbd> <br> Installation <br> </kbd>](#Installation)&ensp;
&ensp;[<kbd> <br> Manual Installation <br> </kbd>](#manual-installation)&ensp;
&ensp;[<kbd> <br> Updating <br> </kbd>](#Updating)&ensp;
&ensp;[<kbd> <br> Meme <br> </kbd>](#Meme)&ensp;
&ensp;[<kbd> <br> Contributors <br> </kbd>](#Contributors)&ensp;
<br><br><br><br></div>
## TODO

- [x] Complete the Essential Pacman Functions
- [x] Ability to Modify Arch Mirrorlist
- [x] Implement AUR Helper
- [x] Better Inspection and Secure AUR Package Installation
- [x] Better Optimization
- [x] Implement Better Command Handling

## Requirements
* Arch Linux
* A Text Editor Set in $EDITOR Path

* Dependencies -
  
   - less
   - pacman
   - curl
   - base-devel
   - make
   - gcc
   - git
   - tar
   - sudo
   - json-c

## Usage

  * Actions :
    - update        - ( Update outdated system/user packages )
    - refresh       - ( Refresh Repository Database )
    - github        - ( Opens GitHub )
    - install       - ( Install a package )
    - install-local - ( Install a local package )
    - install-aur   - ( Install aur package )
    - install-force - ( Forcefully install a package )
    - modify-repo   - ( Modify arch repositories )
    - query         - ( Query if a package is installed )
    - search        - ( Search for a package in the base repository )
    - search-aur    - ( Search for a package in the aur repository )
    - remove        - ( Remove a package )
    - remove-dep    - ( Remove a package along with its dependencies )
    - remove-force  - ( Forcefully remove a package even if other packages depend on it )
    - remove-orp    - ( Remove orphan packages )
    
  * Options :
    - --version, -v - ( Display the version of the package manager )
    - --help,    -h - ( Display this help guide )
## Installation 
 * This methods uses the AUR
   ```bash
   yay -S aurc
   ```
   * This method is encouraged for those that do not want to manually update

## Manual Installation
> [!CAUTION]
> Make sure you are in the SRC Directory
  * Install dependencies : 

      ```bash
      sudo pacman -S less gcc pacman make base-devel git sudo tar curl json-c
      ```

  * Method 1 :

      ```bash
      git clone https://github.com/resslr/aurc.git
      ```
      ```bash
      cd aurc/src
      ```
      ```bash
      sudo make install
      ```

   * Method 2 :

      ```bash
      curl https://github.com/resslr/aurc/releases/latest/download/aurc-pkgbuild.tar.gz -o aurc-pkgbuild.tar.gz
      ```
      ```bash
      tar xvzf aurc-pkgbuild.tar.gz
      ```
      ```bash
      cd aurc-pkgbuild
      ```
      ```bash
      makepkg -si
      ```
   
   * Method 3 :

      ```bash
      wget https://github.com/resslr/aurc/releases/latest/download/aurc-${version}-x86_64.pkg.tar.zst
      ```
      
      or download the file from <a href="https://github.com/statulr/aurc/releases/latest/">Github Releases</a></h1>

      ```bash
      sudo pacman -U aurc-${version}-x86_64.pkg.tar.zst
      ```

## Updating
> [!CAUTION]
> Make sure you are in the SRC Directory
   ```bash
   git fetch & git pull
   ```
   Then
   ```bash
   sudo rm -rd build && sudo make clean install
   ```
### Updating via AUR
```bash
yay
```
or
```bash
aurc update && aurc install-aur aurc
```

## Meme
[![meme](https://media.discordapp.net/attachments/1067268771238129724/1176522320878248036/image.png?ex=656f2ccc&is=655cb7cc&hm=f013e5fb79a07d61671a95b4b7c0b8befb96e5fb8f1141e07f8c08c21b68a600&=&width=438&height=443)](https://www.youtube.com/watch?v=jyARhOtwHUc)

# Contributors
[@xslendix](https://github.com/xslendix)
<br>
[@rifsxd](https://github.com/rifsxd)
<br>
[@umutsevdi ](https://github.com/mutsvedi)
<br>
[@braddotcoffee ](https://github.com/braddotcoffee)
<br>
[@Hamza12700](https://github.com/Hamza12700)
#### If you are opening a pull request add your name here so that if it gets accepted you can automatically be added
