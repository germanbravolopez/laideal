function Set-ShortcutIcon {
    # Replace the paths with your actual shortcut and icon file paths
    $shortcutPath = "C:\Users\Public\Desktop\La Ideal.lnk"
    $iconPath = "lavadora.ico"

    # Check if the shortcut file exists
    if (-not (Test-Path -Path $shortcutPath -PathType Leaf)) {
        Write-Host "Shortcut file not found: $shortcutPath"
        return
    }

    # Check if the icon file exists
    if (-not (Test-Path -Path $iconPath -PathType Leaf)) {
        Write-Host "Icon file not found: $iconPath"
        return
    }

    try {
        # Create a Shell object
        $shell = New-Object -ComObject WScript.Shell

        # Get the shortcut object
        $shortcut = $shell.CreateShortcut($shortcutPath)

        # Set the icon path for the shortcut
        $shortcut.IconLocation = $iconPath

        # Save the changes to the shortcut
        $shortcut.Save()

        Write-Host "Icon changed successfully!"
    } catch {
        Write-Host "An error occurred: $_.Exception.Message"
    }
}

# Call the function to change the shortcut icon
Set-ShortcutIcon
