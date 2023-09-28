#!/usr/bin/env groovy

properties([
  disableConcurrentBuilds(),
])

def iteration = env.BUILD_NUMBER



buildDockerImage {
    namespace = 'coz'
    imageName = 'builder'
    flatten = true
    noPublish = true
    buildParameters = "--build-arg RELEASE=${iteration} -v \$PWD:/output ."
    stash = [includes:"rpms/x86_64/*.rpm"]
}

node() {        
    rpm = new be.kuleuven.icts.Rpm()
    deleteDir()
    stage("unstash") {
        dir("unstash") {
            sh("rm -rf rpms unstash")
            unstash "buildDockerImage"
        }
    }

    stage(name: 'upload rpm') {
        rpmfile = findFiles(glob: 'unstash/rpms/x86_64/*.rpm')
        echo "${rpmfile}"
        rpm.upload_rpms(files: rpmfile, repository: "icts-p-coz-rpm-local/8")
    }
}
