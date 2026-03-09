on open droppedItems
    repeat with droppedFile in droppedItems
        set filePath to POSIX path of droppedFile
        set fileName to name of (info for droppedFile)

        -- Just log what we received
        set logFile to "/tmp/fargo-droplet-test.log"
        set logText to "Dropped: " & fileName & " at " & filePath & " on " & (current date)

        try
            do shell script "echo " & quoted form of logText & " >> " & logFile
        end try

        display notification "Received: " & fileName with title "Test Droplet"
    end repeat
end open

on run
    display notification "Ready to receive drops" with title "Test Droplet"
end run
