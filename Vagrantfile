Vagrant.configure("2") do |config|
    config.vagrant.plugins = ["vagrant-libvirt", "vagrant-sshfs"]

    config.vm.box = "generic/ubuntu2004"

    config.vm.synced_folder "./", "/home/vagrant/hypertrie", type: "rsync"

    config.vm.provision "shell", path: "scripts/install.sh"

    config.vm.provider :libvirt do |libvirt|
        libvirt.cpus = 2
        libvirt.memory = 4096
        libvirt.graphics_type = 'spice'
        libvirt.video_type = 'qxl'
        libvirt.random :model => 'random'
    end
end
