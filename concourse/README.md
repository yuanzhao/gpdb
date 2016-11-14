# Concourse Directory Contract
We are going to be experimenting with a shared ownership of our pipelines and tasks.
This is an unusual configuration, where different teams may be deploying the same pipeline to separate Concourse instances.

Please be careful when updating files in this directory that you know which teams will be impacted.

### Directory Structure
The Concourse directory should contain this README and three sub-directories only:

* pipelines
* tasks
* scripts

##### Pipelines Directory
All pipelines should live in this directory.
Any change to a pipeline yaml with dependent teams must be done in a PR so that teams can update their deployments as needed.
Every pipeline yaml should have a declarative comment at the top of the form:
```
## ======================================================================
## Purpose:
##
## Responsible Team:
##
## Dependent Teams:
##
## ======================================================================
```
Any team that deploys this pipeline and is not the `Responsible Team` should add themselves as a `Dependent Team`.
This will ensure that breaking changes will be vetted by all involved parties.

##### Tasks Directory
All tasks yamls should live in this directory.
If a task file is not referenced in the `pipelines` directory it is considered abandoned and can be removed.
Any change to a task yaml that is used by a pipeline with a `Dependent Team` should be made in a PR.

##### Scripts Directory
All script files should live in this directory.
If a script file is not referenced in any of the directories it is considered abandoned and can be removed.
Any change to a script file that is used by a pipeline with a `Dependent Team` should be made in a PR.

### Updating This README
Any team can propose changes to this contract with a PR.
