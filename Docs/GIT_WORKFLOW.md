# Git Workflow

This directory is managed as a system project with one root repository and nested repositories for vendor code.

## Layout

- Root repo: `C:\Users\davoud\Desktop\octopus`
- Nested Marlin repo: `C:\Users\davoud\Desktop\octopus\marlin-2.1.2.6`
- Nested MarlinConfigurations repo: `C:\Users\davoud\Desktop\octopus\MarlinConfigurations-2.1.2.6`

The root repo should track:

- `ESP_Octopus`
- `ESP_Remote`
- `simulator`
- `gcodes`
- `Docs`
- helper scripts
- the current gitlink state for nested repos

The nested Marlin repos should track their own source changes directly.

## Rule

Commit nested Marlin changes first, then commit the root repo.

That gives you:

- one commit in the Marlin repo with the real firmware changes
- one commit in the root repo that records the updated Marlin pointer plus your ESP/simulator/docs changes

## Manual Flow

```powershell
cd C:\Users\davoud\Desktop\octopus\marlin-2.1.2.6
git status
git add .
git commit -m "Tune spider homing"

cd C:\Users\davoud\Desktop\octopus
git status
git add .
git commit -m "Update Marlin pointer and ESP UI"
```

## Important Notes

- Do not commit `.pio`, `node_modules`, caches, or generated firmware files.
- Do not delete the nested `.git` directories unless you intentionally want to flatten the project structure.
- The root repo is the "system snapshot." The nested Marlin repo is the firmware source history.
- If you later add remotes, push the nested Marlin repo separately from the root repo.

## Status Meaning

In the root repo, this line:

```text
 m marlin-2.1.2.6
```

means the nested Marlin repo changed and the root repo sees that its gitlink pointer no longer matches the last root commit.

That is expected until you commit the root repo after committing Marlin.
