-- Fargo PDF Print Droplet - Simple version (no admin needed)
on open droppedItems
    repeat with droppedFile in droppedItems
        set filePath to POSIX path of droppedFile
        set fileName to name of (info for droppedFile)

        if fileName ends with ".pdf" or fileName ends with ".PDF" then
            set scriptPath to "/Users/jamisonhill/Ai/fargo-dtc4500e-macos-driver/fargo-print-wrapper.sh"
            set shellCmd to scriptPath & " " & filePath

            try
                -- Run without admin privileges (user can access USB directly)
                do shell script shellCmd
                display notification "Sent to Fargo printer" with title "Print Success" subtitle fileName
            on error errMsg
                display notification errMsg with title "Print Failed" subtitle fileName
            end try
        else
            display notification "Only PDF files are supported" with title "Invalid File"
        end if
    end repeat
end open

on run
    display notification "Drag PDF files here to print" with title "Fargo PDF Printer"
end run
